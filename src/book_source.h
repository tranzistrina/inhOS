// Fixed read cache shared by pagination and the native book metadata decoder.
struct Source : inh::TextSource {
  File *f = nullptr;
  uint32_t limit = UINT32_MAX;
  uint32_t cachedAt = UINT32_MAX;
  size_t cachedSize = 0;
  uint8_t cache[512];
  void reset(File *file, uint32_t end = UINT32_MAX) {
    f = file;
    limit = end;
    cachedAt = UINT32_MAX;
    cachedSize = 0;
  }
  uint32_t size() const override {
    return f ? std::min<uint32_t>(f->size(), limit) : 0;
  }
  int byte(uint32_t p) override {
    if (!f || p >= size())
      return -1;
    if (cachedAt == UINT32_MAX || p < cachedAt || p - cachedAt >= cachedSize) {
      cachedAt = p - (p % sizeof cache);
      if (!f->seek(cachedAt)) {
        cachedAt = UINT32_MAX;
        return -1;
      }
      cachedSize =
          f->read(cache, std::min<size_t>(sizeof cache, f->size() - cachedAt));
    }
    return p - cachedAt < cachedSize ? cache[p - cachedAt] : -1;
  }
};
