# Sonic Anthology for Android

The game is the same native code as the PC build (`libmain.so`, run by SDL's
`SDLActivity`); the app's front door, `SetupActivity`, is AnthologyBuilder:
the player picks their own dumps with Android's file picker, and the PC
builder's converters (`tools/trilogy`, run by Chaquopy's Python) make the
game's files on the device. Its look is the title screen's (`TitleView`,
art from `tools/trilogy/android_art.py`).

## Building

Needs the Android SDK (platform 34, NDK 28.2, CMake 3.22.1), JDK 17, Python
3.10 (Chaquopy builds the app's Python with the same version) and the PC game
built once (its recompiler emits the S3K C).

```sh
python android/tools/setup_deps.py      # SDL2's source (verified), its Java glue, the S3K C
cd android
./gradlew assembleDebug -Pabis=arm64-v8a,x86_64     # x86_64: the emulator
./gradlew assembleRelease                           # arm64, signed with keystore.properties
```

`android/local.properties` points at the SDK (`sdk.dir=C:/Android/sdk`).
`originsDir` (gradle.properties) is a folder holding `origins.bin` and
`origins_ufo.bin`, the Sonic Origins sprites pack the APK carries.

Release signing reads `android/keystore.properties` (never committed):

```
storeFile=C:/path/to/sonic-anthology.jks
storePassword=...
keyAlias=anthology
keyPassword=...
```

Keep that keystore: Android only updates an installed app from an APK signed
with the same key.

## On the device

Everything lives in the app's files dir
(`/sdcard/Android/data/io.github.sonsegajp.anthology/files`): the built
files, `settings.ini` (fullscreen and the adaptive widescreen picture on a
first run), saves, and Sonic CD's disc, copied in (the game streams its music
from it). The dumps themselves are kept in the app's private storage, for
rebuilding after an update.

Touch: an on-screen pad (`segagenesisrecomp/runner/touch_overlay.c`), sized
or turned off in Options → Touch Controls; a Bluetooth or USB pad hides it.
Back is Start (the pause menu). The Data Select's zone previews are recorded
by the PC builder playing the game, so on Android the slots show static.
