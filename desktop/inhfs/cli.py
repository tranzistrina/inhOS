import argparse
import json
import sys
from .client import Client, ports


def parser():
    a = argparse.ArgumentParser(description='inhOS: файлы через USB-UART, INHFS/1')
    a.add_argument('--port', help='COM3 или /dev/cu.usbserial-...')
    a.add_argument('--exit-usb', action='store_true', help='Вернуть устройство в обычный режим после команды')
    sub = a.add_subparsers(dest='command', required=True)
    for name in ('list', 'info', 'ports', 'get-sleep-photo', 'cancel', 'disconnect'):
        sub.add_parser(name)
    u = sub.add_parser('upload'); u.add_argument('source'); u.add_argument('--name')
    d = sub.add_parser('download'); d.add_argument('name'); d.add_argument('destination')
    d = sub.add_parser('delete'); d.add_argument('name')
    w = sub.add_parser('wipe'); w.add_argument('--confirm', action='store_true')
    s = sub.add_parser('set-sleep-photo'); s.add_argument('name', help='Путь /photo.ginh или builtin')
    return a


def main(argv=None):
    args = parser().parse_args(argv)
    if args.command == 'ports':
        for port in ports():
            print(port.device, port.description)
        return 0
    client = Client()
    try:
        port = args.port
        if not port:
            candidates = [p for p in ports() if p.vid == 0x10c4 or 'usbserial' in p.device.lower()]
            if len(candidates) != 1:
                raise ValueError('Укажите --port; автоматический выбор требует ровно один CP210x')
            port = candidates[0].device
        client.connect(port)
        progress = lambda n, total: print(f'\r{100*n//max(total,1)}% ({n}/{total})', end='', file=sys.stderr, flush=True)
        command = args.command
        if command == 'info':
            print(json.dumps(client.info(), ensure_ascii=False, indent=2))
        elif command == 'list':
            print(json.dumps(client.files(), ensure_ascii=False, indent=2))
        elif command == 'upload':
            print(client.upload(args.source, args.name, progress))
        elif command == 'download':
            client.download(args.name, args.destination, progress)
        elif command == 'delete':
            client.delete(args.name)
        elif command == 'wipe':
            client.wipe(args.confirm)
        elif command == 'get-sleep-photo':
            print(client.sleep_photo() or 'builtin')
        elif command == 'set-sleep-photo':
            print(client.sleep_photo('' if args.name == 'builtin' else args.name) or 'builtin')
        elif command == 'cancel':
            from .protocol import CANCEL
            client.request(CANCEL)
        return 0
    except Exception as e:
        print(f'Ошибка: {e}', file=sys.stderr)
        return 1
    finally:
        # Default CLI keeps USB mode ready for the next automated command.
        client.close(graceful=args.exit_usb or args.command == 'disconnect')


if __name__ == '__main__':
    raise SystemExit(main())
