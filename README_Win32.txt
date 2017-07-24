		       Using AFF Tool Under Microsoft Windows (Win32)


There may have been pre-compiled executables of AFFLIB available for
download once upon a time (not sure as I have never seen them).  But
currently there is not any, and has not been for a long time.

Also, the steps below to compile from source have not been verified
and may be horribly outdated.  Use at your own risk.

Phillip Hellewell
July 23, 2017


*******************************
Compiling under Windows

There are two ways to compile for Windows:
1 - Compiling natively on Windows using MSVC. (Works for library but not tools).
2 - Compiling natively on Windows using mingw. (UNTESTED)


Compiling natively on Windows with MSVC:
****************************************
See win32/README_MSVC++.txt 

Note: I personally have not verified the steps in that readme, and it too is
quite outdated and may have errors.

I do know that the library itself (afflib.lib) does build with MSVC.  I have
built it myself with both VS2010 and VS2015.  However, the tools I have not
built, and last I heard someone tried and did not succeed because they rely
on a windows version of getopt.c (not provided).

Phillip Hellewell
July 23, 2017


Compiling natively on Windows with MINGW:  (UNTESTED)
*****************************************

  Download the Windows Resource Kit from:
  http://www.microsoft.com/downloads/details.aspx?familyid=9d467a69-57ff-4ae7-96ee-b18c4790cffd&displaylang=en

  Download and run mingw-get-inst-20101030.exe (or whatever version is current),
  selecting all options including these:
    C Compiler, C++ Compiler. MSYS Basic System, MinGW Development Toolkit.
  When selecting the installation path to MinGW, Do not define a path with spaces in it.

  Start the MinGW32 shell window.

  Download the latest repository catalog and update and install modules required by MinGW
  by typing the following:
  mingw-get update
  mingw-get install g++
  mingw-get install pthreads
  mingw-get install mingw32-make
  mingw-get install zlib
  mingw-get install libz-dev

  Install the libraries in this order:
    * expat (http://sourceforge.net/projects/expat/)
    * openssl (http://openssl.org)

  For each library:
   - download
   - ./configure --prefix=/usr/local/ --enable-winapi=yes
   - make
   - make install

   For openssl, run "./config --prefix=/usr/local" rather than configure.

   Don't make directories in your home directory if there is a space in it! 
   Libtool doesn't handle paths with spaces in them.

  If OpenSSL is installed in /usr/local/ssl, you may need to build other libraries with:
  ./configure CPPFLAGS="-I/usr/local/include" -I/usr/local/ssl/include" \
              LDFLAGS="-L/usr/local/lib -L/usr/local/ssl/lib"

  Most libraries will install in /usr/local/ ; you may need to add -I/usr/local/include to CFLAGS
  and -L/usr/local/lib to your make scripts

  Still problematic, though, is actually running what is produced. Unless you link -static you will have
  a lot of DLL references. Most of the DLLs are installed in /usr/local/bin/*.dll and /bin/*.dll and elsewhere,
  which maps typically to c:\mingw\msys\1.0\local\bin and c:\mingw\bin\


Compiling your own copy:
=======================
We compile with mingw. Download and install MSys. 

Next you will need to download and i


Working with the tools
======================

If you are working with an encrypted disk image, set the environment
variable AFFLIB_PASSPHRASE to be the passphrase that should be used
for decryption.

   % set AFFLIB_PASSPHRASE="this_is_my_passphrase"

Displaying the metadata with a disk image:

   % afinfo.exe filename.aff	  

To convert an AFF file into a RAW file, use:

   % affconvert.exe -e raw filename.aff


To reliably copy an AFF file from one location to another:

   % afcopy.exe  file1.aff  d:\dest\path\file2.aff


To compare two AFF files:

   % afcompare file1.aff file2.aff


To fix a corrupted AFF file:

  % affix badfile.aff


To print statistics about a file:

  % afstats.exe filename.aff



Diskprint
=================
An exciting feature in AFF 3.5 is the ability to rapidly calculate and
verify the "print" of a disk image. A print is constructed by
computing the SHA-256 of the beginning, end, and several randomly
chosen parts of the disk image.

To calculate the diskprint and store it in a file:

   % afdiskprint myfile.iso > myfile.xml

To verify a diskprint

   % afdiskprint -x myfile.xml myfile.iso


Verifying the AFFLIB Digital Signature

