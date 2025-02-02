Compiling YAOG
===============

This describes openSSL & Yaog compilation on Mac OS/X Sequoia (15.2)
Needed software
----------------

* Qt : Need version > 6.7.0 for openSSL 3.4.0 support
Any 3.0 version of openssl should work
I use homebrew to install openssl - brew install openssl
Apple developer tools installed (XCode), which provides clang

-----


Compile OpenSSL
---------------
Homebrew takes care of building openssl

```

Compile with Qt
---------------

run qmake in the directory containing YetAnotherOpensslGui.pro
qmake
make
or
qmake CONFIG+=release
make

The app will be created in YetAnotherOpensslGui.app 
Double click it, and set permissions to allow it to run in Security Settings
To run from the command line, run 
./YetAnotherOpensslGui.app/Contents/MacOS/YetAnotheropensslGui

Running as an app is buggy -- the qt menus don't pull down... I haven't figured this out yet.
Running from the command line works.

To add the qt framework to the application, run
macdeployqt YetAnotherOpensslGui.app -dmg
 


