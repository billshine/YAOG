# YAOG

Current version :  2  using openSSL 3.4.0 and QT 6.8.1
This is a port of PatrickPR/YAOG.  This project seems to have been abandoned,
but with a bit of tweaking, I have it working on OSX using openssl 3.4.0 and
QT 6.8.1

You will need to install both Openssl 3.4.0, and the community edition of QT.
Once installed, you will have to modify YetAnotherOpensslGui.pro to use your 
version of Openssl.

To build, you run qmake first:
qmake
or for a release build, 
qmake CONFIG+=Release

Then
make

The app will run with OS/X 15.0 or higher.  To run with an older version, 
modify the qmake MACOSX_DEPLOYMENT_TARGET to the version you want.

QMAKE_MACOSX_DEPLOYMENT_TARGET = 15
is what it is currently set to

To install openssl, use homebrew.  Homebrew installs openssl  in /opt/homebrew/ 



Features :
- Create auto sign certificates or CSR with immediate PEM display to copy/paste
- Certificate signing
- Stack to handle multiple certificates
- Conversion from certificate (private key) to csr
- Allow RSA, DSA and elliptic curve keys
- Encrypt/decrypt keys, check certificate / key match
- Set X509v3 extensions
- Import/export to PKCS#12
- Currently works on os/x ... Should compile on Linux, preferably with clang.
- Should also build on windows -- I haven't tried building on windows or linux yet 


