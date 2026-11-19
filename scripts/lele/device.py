#!/usr/bin/env python3
"""ESP32-S3 backup/flash helper. Flash requires explicit --confirm-write.

An agent must obtain current user authorization before passing that flag.
No credentials or raw flash contents are printed or uploaded.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[2]
FLASH_SIZE = 16 * 1024 * 1024
NVS_START, NVS_END = 0x9000, 0xD000


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def select_port(explicit=None):
    if explicit:
        return explicit
    ports = [p.device for p in list_ports.comports() if p.vid == 0x303A and p.pid == 0x1001]
    if len(ports) != 1:
        raise RuntimeError('Specify --port; ESP USB candidates: ' + repr(ports))
    return ports[0]


def load_flash_files(build):
    data = json.loads((build / 'flasher_args.json').read_text())
    files = []
    for address, name in sorted(data['flash_files'].items(), key=lambda item: int(item[0], 16)):
        start = int(address, 16)
        path = (build / name).resolve()
        if not path.is_relative_to(build.resolve()) or not path.is_file():
            raise ValueError('Invalid build file: ' + name)
        size = path.stat().st_size
        end = start + size
        sector_start, sector_end = start // 4096 * 4096, (end + 4095) // 4096 * 4096
        if size == 0 or start < 0 or sector_end > FLASH_SIZE:
            raise ValueError('Flash range outside 16 MiB')
        if sector_start < NVS_END and sector_end > NVS_START:
            raise ValueError('Refusing a write overlapping NVS')
        files.append({'address': start, 'file': name, 'bytes': size, 'sha256': digest(path)})
    if {entry['address'] for entry in files} != {0, 0x8000, 0xD000, 0x20000, 0x800000}:
        raise ValueError('Unexpected partition layout; re-review before adapting this helper')
    for left, right in zip(files, files[1:]):
        if (left['address'] + left['bytes'] + 4095) // 4096 * 4096 > right['address']:
            raise ValueError('Overlapping flash ranges')
    app = (build / next(x['file'] for x in files if x['address'] == 0x20000)).read_bytes()
    if app[0] != 0xE9 or int.from_bytes(app[12:14], 'little') != 9 or b'lichuang-dev-lele' not in app:
        raise ValueError('Expected a Lele ESP32-S3 application image')
    if len(app) > 0x3F0000:
        raise ValueError('Application exceeds OTA slot')
    return files


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('action', choices=['ports', 'backup', 'flash'])
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--confirm-write', action='store_true')
    args = parser.parse_args()
    if args.action == 'ports':
        for port in list_ports.comports():
            if port.vid == 0x303A:
                print(port.device, port.description)
        return
    if args.action == 'flash' and not args.confirm_write:
        parser.error('Obtain user approval, then pass --confirm-write; nothing was written.')
    build = args.build.resolve()
    files = load_flash_files(build) if args.action == 'flash' else None
    port = select_port(args.port)
    stamp = datetime.now().astimezone().strftime('%Y%m%d-%H%M%S-%f')
    folder = ROOT / '.local/backups' / stamp
    folder.mkdir(parents=True, mode=0o700)
    flash = folder / 'flash-16MiB.bin'
    nvs = folder / 'nvs.bin'
    command = [sys.executable, '-u', '-m', 'esptool', '--chip', 'esp32s3', '--port', port,
               '--baud', '921600', '--after', 'no-reset']
    with (folder / 'esptool.log').open('wb') as log:
        def run(extra):
            subprocess.run(command + extra, stdout=log, stderr=subprocess.STDOUT, check=True)
        print('Reading full 16 MiB flash and verifying against the device...', flush=True)
        run(['read-flash', '0', hex(FLASH_SIZE), str(flash)])
        if flash.stat().st_size != FLASH_SIZE:
            raise RuntimeError('Incomplete backup; nothing will be written')
        run(['verify-flash', '0', str(flash)])
        nvs.write_bytes(flash.read_bytes()[NVS_START:NVS_END])
        record = {'backup': flash.name, 'sha256': digest(flash), 'bytes': FLASH_SIZE,
                  'full_backup_verified': True, 'write_performed': False}
        (folder / 'result.json').write_text(json.dumps(record, indent=2))
        if files:
            # Keep an inspectable manifest before any write, and require the existing partition table.
            partition = next(x for x in files if x['address'] == 0x8000)
            if flash.read_bytes()[0x8000:0x8000+partition['bytes']] != (build/partition['file']).read_bytes():
                raise RuntimeError('Partition table differs; review migration separately')
            (folder / 'write-manifest.json').write_text(json.dumps(files, indent=2))
            write_args = ['write-flash', '--flash-mode', 'dio', '--flash-size', '16MB', '--flash-freq', '80m']
            for entry in files:
                path = build / entry['file']
                if digest(path) != entry['sha256']:
                    raise RuntimeError('Build changed while backing up; nothing will be written')
                write_args += [hex(entry['address']), str(path)]
            print('Verified backup. Writing five partition files...', flush=True)
            run(write_args)
            run(['verify-flash', hex(NVS_START), str(nvs)])
            record.update(write_performed=True, nvs_unchanged_before_boot=True, flash_files=files)
            (folder / 'result.json').write_text(json.dumps(record, indent=2))
            subprocess.run(command[:-2] + ['--after', 'hard-reset', 'flash-id'],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        else:
            print('Device remains in download mode; press RESET to resume normal boot.')
    print('Private backup and verification record: ' + str(folder))


if __name__ == '__main__':
    main()
