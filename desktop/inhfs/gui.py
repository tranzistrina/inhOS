"""Russian Tk GUI. All serial I/O runs outside the Tk thread."""
import queue
import threading
import time
from pathlib import Path
from .client import Client, ports


def self_test(platform):
    from .protocol import packet, unpack, frame, Decoder
    from .images import encode, decode
    from .cli import parser
    raw = packet(1, 123, data=b'INHFS/1')
    assert Decoder().feed(frame(raw)) == [raw]
    assert unpack(raw)[1] == 123
    assert len(decode(encode(bytes(3813)))) == 3813
    assert parser().parse_args(['list']).command == 'list'
    print(f'{platform}: entry point/imports/protocol/converter/CLI OK (not a native GUI or hardware test)')
    return 0


def main(platform=''):
    import sys
    if '--self-test' in sys.argv:
        return self_test(platform)
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    root = tk.Tk()
    root.title('inhOS — USB-файлы' + (' · ' + platform if platform else ''))
    root.geometry('880x600')
    root.minsize(760, 520)
    client = Client()
    events = queue.Queue()
    busy = False
    closing = False
    last_progress = 0.0
    port_value = tk.StringVar()
    status = tk.StringVar(value='На устройстве: Настройки → USB-файлы. Подключите USB-кабель.')
    details = tk.StringVar(value='Нет подключения')
    sleep = tk.StringVar(value='Заставка сна: —')
    top = ttk.Frame(root, padding=12); top.pack(fill='x')
    ttk.Label(top, text='Порт:').pack(side='left')
    combo = ttk.Combobox(top, textvariable=port_value, width=32); combo.pack(side='left', padx=8)
    actions = []

    def button(parent, label, fn):
        b = ttk.Button(parent, text=label, command=fn)
        b.pack(side='left', padx=3, pady=3)
        actions.append(b)
        return b

    def scan():
        try:
            candidates = ports()
            combo['values'] = [p.device for p in candidates]
            if not port_value.get() and candidates:
                port_value.set(candidates[0].device)
            status.set('CP210x показаны первыми; порт также можно ввести вручную.')
        except Exception as e:
            messagebox.showerror('Порты', str(e))

    def state():
        return client.info(), client.files(), client.sleep_photo()

    def apply_state(result):
        info, files, photo = result
        for item in tree.get_children():
            tree.delete(item)
        types = {'app': 'Приложение', 'photo': 'Фотография', 'book': 'Книга'}
        for item in files:
            tree.insert('', 'end', iid=item['name'], values=(item['name'], types.get(item['type'], item['type']), f"{item['size']:,}"))
        details.set(f"inhOS {info['version']} · свободно {info['free']/1048576:.2f} / {info['total']/1048576:.2f} МиБ · INHFS/1")
        sleep.set('Заставка сна: ' + (photo or 'встроенная'))

    def run(label, operation, update=True):
        nonlocal busy
        if busy:
            return
        busy = True
        client.cancel_event.clear()
        for b in actions:
            b['state'] = 'disabled'
        combo['state'] = 'disabled'
        status.set(label)
        progress['value'] = 0
        def work():
            try:
                result = operation()
                events.put(('done', (label, result if update else None)))
            except Exception as e:
                events.put(('error', str(e)))
        threading.Thread(target=work, daemon=True).start()

    def connect():
        if client.serial is not None:
            messagebox.showinfo('Подключение', 'Соединение уже установлено. Для смены порта сначала отключитесь.')
            return
        port = port_value.get().strip()
        if not port:
            messagebox.showerror('Подключение', 'Выберите порт')
            return
        def work():
            client.close()
            client.connect(port)
            return state()
        run('Подключение…', work)

    def disconnect():
        run('Отключение…', lambda: client.close(), False)

    button(top, 'Найти', scan)
    button(top, 'Подключить', connect)
    button(top, 'Отключить', disconnect)
    ttk.Label(root, textvariable=details, padding=(14, 4)).pack(anchor='w')
    ttk.Label(root, textvariable=sleep, padding=(14, 4)).pack(anchor='w')
    middle = ttk.Frame(root, padding=12); middle.pack(fill='both', expand=True)
    tree = ttk.Treeview(middle, columns=('name', 'type', 'size'), show='headings', selectmode='extended')
    for col, title, width in [('name', 'Имя файла', 400), ('type', 'Тип', 160), ('size', 'Размер, байт', 120)]:
        tree.heading(col, text=title); tree.column(col, width=width)
    scroll = ttk.Scrollbar(middle, orient='vertical', command=tree.yview)
    tree.configure(yscrollcommand=scroll.set)
    tree.pack(side='left', fill='both', expand=True); scroll.pack(side='right', fill='y')

    def selected(one=False):
        names = tree.selection()
        if not names or (one and len(names) != 1):
            messagebox.showinfo('Файлы', 'Выберите один файл' if one else 'Выберите файлы')
            return ()
        return names

    def on_progress(done, total):
        nonlocal last_progress
        now = time.monotonic()
        if now-last_progress > .08 or done == total:
            events.put(('progress', (done, total)))
            last_progress = now

    def upload():
        names = filedialog.askopenfilenames(title='Загрузить файлы (TXT/FB2 заменяют текущую книгу)', filetypes=[('Файлы inhOS', '*.inh *.ginh *.txt *.fb2 *.jpg *.jpeg *.png *.webp'), ('Все файлы', '*')])
        if not names:
            return
        if any(Path(n).suffix.lower() in ('.txt', '.fb2') for n in names) and not messagebox.askyesno('Книга', 'Загруженная книга заменит текущую. Продолжить?'):
            return
        def work():
            for name in names:
                client.check_cancel()
                client.upload(name, progress=on_progress)
            return state()
        run('Загрузка…', work)

    def download():
        names = selected()
        if not names:
            return
        if len(names) == 1:
            target = filedialog.asksaveasfilename(initialfile=names[0][1:], title='Скачать файл')
            targets = [target] if target else []
        else:
            target = filedialog.askdirectory(title='Папка для скачивания')
            targets = [str(Path(target) / n[1:]) for n in names] if target else []
            if targets and any(Path(t).exists() for t in targets) and not messagebox.askyesno('Замена файлов', 'Некоторые файлы уже существуют. Заменить?'):
                return
        if not targets:
            return
        def work():
            for name, target in zip(names, targets):
                client.check_cancel()
                client.download(name, target, on_progress)
            return state()
        run('Скачивание…', work)

    def delete():
        names = selected()
        if names and messagebox.askyesno('Удаление', 'Удалить выбранные файлы? Книга удаляется вместе с индексами.\n\n' + '\n'.join(names)):
            def work():
                for name in names:
                    client.delete(name)
                return state()
            run('Удаление…', work)

    def wipe():
        if messagebox.askyesno('Полная очистка', 'Удалить ВСЕ приложения, фотографии и книгу с индексами? Настройка разворота сохраняется.', icon='warning'):
            def work():
                client.wipe(True)
                return state()
            run('Очистка…', work)

    def set_sleep(builtin=False):
        names = ('',) if builtin else selected(True)
        if names:
            def work():
                client.sleep_photo(names[0])
                return state()
            run('Выбор заставки…', work)

    row = ttk.Frame(root, padding=(10, 0)); row.pack(fill='x')
    button(row, 'Загрузить / фото…', upload)
    button(row, 'Скачать…', download)
    button(row, 'Удалить', delete)
    button(row, 'Обновить', lambda: run('Обновление…', state))
    button(row, 'Очистить всё', wipe)
    row2 = ttk.Frame(root, padding=(10, 0)); row2.pack(fill='x')
    button(row2, 'Выбранное фото для сна', set_sleep)
    button(row2, 'Встроенная заставка', lambda: set_sleep(True))
    ttk.Button(row2, text='Отменить передачу', command=client.cancel_transfer).pack(side='left', padx=8)
    progress = ttk.Progressbar(root, maximum=100); progress.pack(fill='x', padx=14, pady=8)
    ttk.Label(root, textvariable=status, wraplength=820, padding=(14, 4)).pack(anchor='w')
    ttk.Label(root, text='Без Wi-Fi · На устройстве: 3 клика или удержание — выход из USB', padding=(14, 8)).pack(anchor='w')

    def on_close():
        nonlocal closing
        closing = True
        client.cancel_transfer()
        if not busy:
            run('Закрытие порта…', lambda: client.close(), False)
    root.protocol('WM_DELETE_WINDOW', on_close)

    def poll():
        nonlocal busy
        try:
            while True:
                kind, value = events.get_nowait()
                if kind == 'progress':
                    done, total = value
                    progress['value'] = 100*done/max(total, 1)
                    status.set(f'Передано {done:,} / {total:,} байт')
                else:
                    busy = False
                    for b in actions:
                        b['state'] = 'normal'
                    combo['state'] = 'normal'
                    if kind == 'error':
                        status.set(value)
                        if not closing:
                            messagebox.showerror('inhOS', value)
                    else:
                        label, result = value
                        if result is not None:
                            apply_state(result)
                        status.set('Готово')
                    if client.serial is None:
                        details.set('Нет подключения')
                    if closing:
                        if client.serial is not None:
                            run('Закрытие порта…', lambda: client.close(), False)
                        else:
                            root.destroy()
                            return
        except queue.Empty:
            pass
        root.after(50, poll)

    def heartbeat():
        if client.serial is not None and not busy and not closing:
            run('Проверка соединения…', lambda: client.ping(), False)
        root.after(8000, heartbeat)
    scan(); poll(); root.after(8000, heartbeat)
    root.mainloop()
    return 0
