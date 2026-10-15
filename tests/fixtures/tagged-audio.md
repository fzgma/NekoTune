`tagged.flac` is a generated 250 ms mono silent recording for offline metadata tests.
It contains UTF-8 title/artist/album, album artist, genre, date, fractional track/disc numbers and lyrics.
No external recording or network service is used. Tests read the checked-in fixture and do not require the FFmpeg CLI.

Regenerate from the repository root with:

```sh
ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=8000:cl=mono -t 0.25 \
  -metadata title='夜空 02' -metadata artist='示例歌手' -metadata album='月光专辑' \
  -metadata album_artist='Various Artists' -metadata genre='Pop' \
  -metadata date='2024-05-10' -metadata track='2/12' -metadata disc='1/2' \
  -metadata lyrics='Embedded lyrics' -bitexact tests/fixtures/tagged.flac
```
