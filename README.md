# iBroadcast Uploader

Upload audio files to [ibroadcast.com](https://www.ibroadcast.com).

## Compile

Do not add optimization flag (e.g., `-O2`) because it will cause `stack smashing detected` error, which I do not know why yet.
`gcc -o ibup ibroadcast-uploader.c -lcurl -lssl -ljansson -lcrypto -lpthread`
