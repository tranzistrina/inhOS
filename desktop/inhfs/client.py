"""Streaming serial client shared by Windows/macOS GUI and CLI."""
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import threading
import time
import zlib
from . import protocol as p


class InhError(Exception):
    pass


class Cancelled(InhError):
    pass


def ports():
    from serial.tools import list_ports
    all_ports = list(list_ports.comports())
    return sorted(all_ports, key=lambda x: (x.vid != 0x10c4, 'usbserial' not in x.device.lower(), x.device))


def remote_path(name):
    if not re.fullmatch(r'/[A-Za-z0-9_.-]{1,36}', name) or '..' in name:
        raise InhError('Недопустимое имя файла')
    if name != '/book.txt' and not name.endswith(('.inh', '.ginh')):
        raise InhError('Доступны только book.txt, .inh и .ginh')
    return name.encode('ascii')


def crc_file(file):
    crc = 0
    while True:
        block = file.read(65536)
        if not block:
            break
        crc = zlib.crc32(block, crc)
    file.seek(0)
    return crc


def prepare(source, directory):
    """Convert locally; never transmit JPG/FB2 as interpreted app source."""
    from .images import convert, decode
    source = Path(source)
    ext = source.suffix.lower()
    name = re.sub('[^A-Za-z0-9_-]', '_', source.stem)[:30] or 'file'
    if ext in ('.jpg', '.jpeg', '.png', '.webp'):
        out = Path(directory) / (name + '.ginh')
        convert(source, out)
        return out, '/' + out.name
    if ext == '.fb2':
        import xml.etree.ElementTree as ET
        if source.stat().st_size > 16 * 1024 * 1024:
            raise InhError('FB2 превышает 16 МиБ')
        raw = source.read_bytes()
        # Reject DTD/entities also in UTF-16 documents before parsing.
        check = raw.replace(b'\x00', b'').upper()
        if b'<!DOCTYPE' in check or b'<!ENTITY' in check:
            raise InhError('DTD и сущности в FB2 запрещены')
        root = ET.fromstring(raw)
        paragraphs = []
        for body in root.iter():
            if body.tag.split('}')[-1] == 'body':
                for el in body.iter():
                    if el.tag.split('}')[-1] in ('p', 'v', 'subtitle'):
                        paragraphs.append(''.join(el.itertext()))
        out = Path(directory) / 'book.txt'
        out.write_text('\n'.join(paragraphs) + '\n', encoding='utf-8')
        return out, '/book.txt'
    if ext == '.txt':
        return source, '/book.txt'
    if ext == '.ginh':
        if source.stat().st_size != 3833:
            raise InhError('GINH должен содержать ровно 3833 байта')
        decode(source.read_bytes())
        return source, '/' + name + ext
    if ext == '.inh':
        return source, '/' + name + ext
    raise InhError('Выберите TXT, FB2, INH, GINH, PNG, JPG или WebP')


class Client:
    def __init__(self, transport=None):
        self.serial = transport
        self.session = 0
        self.seq = 1
        self.decoder = p.Decoder()
        self.lock = threading.RLock()
        self.cancel_event = threading.Event()

    def connect(self, port=None):
        with self.lock:
            if self.serial is None:
                import serial
                try:
                    s = serial.Serial(port=None, baudrate=115200, timeout=.05, write_timeout=3)
                    s.dtr = False
                    s.rts = False
                    if os.name != 'nt':
                        s.exclusive = True
                    s.port = port
                    s.open()
                    self.serial = s
                except Exception as e:
                    try:
                        s.close()
                    except Exception:
                        pass
                    raise InhError(f'Не удалось открыть порт (возможно, занят): {e}') from e
            self.session, self.seq = 0, 1
            self.decoder = p.Decoder()
            try:
                self.request(p.HELLO, b'INHFS/1', timeout=2, retries=12)
                return self.info()
            except Exception:
                self.close(graceful=False)
                raise

    def request(self, op, data=b'', block=0, timeout=2, retries=5):
        with self.lock:
            if self.serial is None:
                raise InhError('Порт не подключён')
            raw = p.frame(p.packet(op, self.seq, block, self.session, data))
            try:
                for attempt in range(retries):
                    if self.serial.write(raw) != len(raw):
                        raise OSError('Неполная запись в порт')
                    deadline = time.monotonic() + timeout
                    retry = False
                    while time.monotonic() < deadline:
                        waiting = getattr(self.serial, 'in_waiting', None)
                        received = self.serial.read(1024 if waiting is None else max(1, min(1024, waiting)))
                        for response in self.decoder.feed(received):
                            try:
                                rop, seq, rb, session, status, payload = p.unpack(response)
                            except ValueError:
                                continue
                            if rop != op | 0x80 or seq != self.seq or rb != block:
                                continue
                            if op != p.HELLO and session != self.session:
                                continue
                            if status == 2:  # CRC NACK: same request/sequence again
                                retry = True
                                break
                            if status in (3, 4):
                                raise InhError('Устройство перезагрузилось или сеанс потерян. Подключитесь заново; передача не продолжена.')
                            self.seq = (self.seq + 1) & 0xffffffff
                            if status:
                                errors = {1: 'Неверный пакет', 5: 'Недопустимый путь', 6: 'Устройство занято', 7: 'Ошибка файла/Flash', 8: 'Неверный формат или размер файла', 9: 'Недостаточно места для безопасной замены', 10: 'Требуется подтверждение удаления', 11: 'CRC32 файла не совпадает; старый файл сохранён'}
                                raise InhError(errors.get(status, f'Ошибка устройства {status}'))
                            if op == p.HELLO:
                                if payload != b'INHFS/1' or not session:
                                    raise InhError('Несовместимая прошивка')
                                self.session = session
                            return payload
                        if retry:
                            break
                raise OSError('Нет подтверждения INHFS/1. Выберите «Настройки → USB-файлы» на устройстве и повторите подключение.')
            except (OSError, InhError) as e:
                # Unknown delivery state after timeout: don't guess the next seq.
                if isinstance(e, OSError) or 'сеанс потерян' in str(e):
                    self.close(graceful=False)
                if isinstance(e, InhError):
                    raise
                raise InhError(f'Связь прервана: {e}') from e

    def info(self):
        return json.loads(self.request(p.INFO))

    def files(self):
        with self.lock:
            result = []
            for i in range(10000):
                data = self.request(p.LIST, block=i)
                if not data:
                    return result
                item = json.loads(data)
                remote_path(item['name'])
                result.append(item)
            raise InhError('Слишком много файлов')

    def check_cancel(self):
        if self.cancel_event.is_set():
            raise Cancelled('Передача отменена')

    def cancel_transfer(self):
        self.cancel_event.set()

    def upload(self, source, remote=None, progress=lambda done, total: None):
        with self.lock, tempfile.TemporaryDirectory() as tmp:
            self.cancel_event.clear()
            local, default = prepare(source, tmp)
            name = remote_path(remote or default)
            total = local.stat().st_size
            if not total or total > (3145728 if name == b'/book.txt' else 32768):
                raise InhError('Файл пуст или превышает допустимый размер')
            begun = False
            try:
                with open(local, 'rb') as f:
                    crc = crc_file(f)
                    self.request(p.PUT_BEGIN, struct.pack('<II', total, crc) + name)
                    begun = True
                    offset = 0
                    while offset < total:
                        self.check_cancel()
                        chunk = f.read(512)
                        self.request(p.PUT_DATA, chunk, offset)
                        offset += len(chunk)
                        progress(offset, total)
                    self.check_cancel()
                    self.request(p.PUT_END, timeout=60)
                    begun = False
                return name.decode()
            finally:
                if begun and self.serial is not None:
                    self.request(p.CANCEL)

    def download(self, remote, destination, progress=lambda done, total: None):
        with self.lock:
            self.cancel_event.clear()
            destination = Path(destination)
            temporary = None
            begun = False
            try:
                meta = self.request(p.GET_BEGIN, remote_path(remote), timeout=60)
                begun = True
                total, expected = struct.unpack('<II', meta)
                if total > 3145728:
                    raise InhError('Недопустимый размер файла')
                with tempfile.NamedTemporaryFile(dir=destination.parent, prefix='.' + destination.name + '.', suffix='.part', delete=False) as f:
                    temporary = Path(f.name)
                    offset = crc = 0
                    while offset < total:
                        self.check_cancel()
                        chunk = self.request(p.GET_DATA, block=offset)
                        if not chunk or len(chunk) > total-offset:
                            raise InhError('Повреждённая длина блока')
                        f.write(chunk)
                        crc = zlib.crc32(chunk, crc)
                        offset += len(chunk)
                        progress(offset, total)
                    f.flush()
                    os.fsync(f.fileno())
                if crc != expected:
                    raise InhError('CRC32 скачанного файла не совпадает')
                self.check_cancel()
                os.replace(temporary, destination)
                temporary = None
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
                if begun and self.serial is not None:
                    self.request(p.CANCEL)

    def delete(self, remote):
        return self.request(p.DELETE, remote_path(remote))

    def wipe(self, confirmed=False):
        if not confirmed:
            raise InhError('Полная очистка требует подтверждения')
        return self.request(p.WIPE, b'DELETE USER FILES', timeout=60)

    def sleep_photo(self, name=None):
        if name is None:
            return self.request(p.SLEEP_GET).decode('ascii')
        if name and not name.endswith('.ginh'):
            raise InhError('Заставкой может быть только GINH')
        return self.request(p.SLEEP_SET, remote_path(name) if name else b'').decode('ascii')

    def ping(self):
        self.request(p.PING)

    def close(self, graceful=True):
        with self.lock:
            s = self.serial
            if s is None:
                return
            try:
                if graceful and self.session:
                    self.request(p.BYE)
            except Exception:
                pass
            finally:
                self.serial = None
                self.session = 0
                s.close()
