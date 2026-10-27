"""Give this owned test launch an inspectable preview, preserving render settings."""
import json


class PreviewSettings:
    TEST_WINDOW = dict(window_width=1280, window_height=720)

    def __init__(self, path):
        self.path = path
        self.original_bytes = self.original = None

    def start(self):
        if self.original is not None:
            raise RuntimeError('Preview settings already owned')
        raw = self.path.read_bytes()
        original = json.loads(raw)
        if not isinstance(original, dict) or any(type(original.get(key)) is not int for key in self.TEST_WINDOW):
            raise ValueError('Simulator preview dimensions are missing or invalid')
        self.original_bytes, self.original = raw, original
        self.path.write_text(json.dumps(dict(original, **self.TEST_WINDOW), indent=2) + '\n', encoding='utf-8')
        return dict(original={key: original[key] for key in self.TEST_WINDOW}, temporary=self.TEST_WINDOW.copy())

    def restore(self):
        if self.original is None:
            return False
        current = json.loads(self.path.read_bytes())
        if not isinstance(current, dict):
            raise ValueError('Simulator settings became invalid; refusing to overwrite them')
        current.update({key: self.original[key] for key in self.TEST_WINDOW})
        if current == self.original:
            self.path.write_bytes(self.original_bytes)
        else:
            # Preserve unrelated settings changed while the owned launch ran.
            self.path.write_text(json.dumps(current, indent=2) + '\n', encoding='utf-8')
        self.original = self.original_bytes = None
        return True
