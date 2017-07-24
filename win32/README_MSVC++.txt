       Building AFFLIB under Windows / Microsoft Visual C++


The instructions below were written for compiling AFFLIB 3.4.2
with MSVC 2008, and were declared obsolete at version 3.6.0
in favor of building with MinGW.

But I personally have never built AFFLIB with MinGW, whereas
I have built with MSVC 2010 and even MSVC 2015.  Though I did
not use the steps below, so they have not been verified and
may be horribly outdated and broken.  Use at your own risk.

The library itself (afflib.lib) does build with MSVC, but the tools
may not.  Last I heard someone tried and did not succeed because
they rely on a windows version of getopt.c (not provided).

Phillip Hellewell
July 23, 2017

================================================================

Introduction
============
This directory builds the following as a single library using
Microsoft Visual C++ that contains all of the following:

     * All of LIBAFF
     * LZMA compression system
     * ZLIB compression system
     * LIBEWF EnCase image reading system.
     * AFFLIB3.0 encryption issues

On Unix systems the ZLIB and LIBEWF libraries must be separately
installed. However, copies of these libraries are included as
subdirectories to the win32 library. These copies are included solely
to make things easier for Windows users; these copies are not used by
the Unix AFFLIB installation.

Windows programs that are linked with this library can read files in
any of the following formats:

    * RAW & Split raw
    * AFF, AFM, AFD
    * EnCase / Expert Witness

You can also use AFFLIB on Windows with Cygwin; details on that
appear in this file as well.


Compiling with Microsoft VC++
=============================

To compile this library, you need a copy of Microsoft Visual C++ 2008 Express.
(Libewf will not compile with any earlier version of Microsoft Visual C++.)


Installing VC++ 2008:
---------------------
You can download a FREE copy of Visual C++ 2005 Express Edition from
Microsoft: http://www.microsoft.com/express

1. Go to http://www.microsoft.com/express

2. Download Visual Studio C++ 2008 Express Edition.  
   (You will get vcsetup.exe; save it on the desktop and run it.)

3. Install in the default location, 
   C:\Program Files\Microsoft Visual Studio 9\

5. Follow the instructions:
   - Run Visual Studio C++ 2008.
   - Select "Register Product" from the help menu.
   - Log into the Microsoft website with your Passport credentials.
   - Get the registration key from Microsoft (after email answerback)
     and paste it into the Help panel.

6. Run Microsoft Update to install the latest service packs.
   - You MUST have the most recent .NET Framework.
   - You should also have the Security Updates for the VC++ 2008
     Redistributable Package

7. Now you must download and install the Microsoft Platform SDK so
   that you will have the header files for the Microsoft Crypto API.
   This can be confusing. I downloaded the Windows Server 2008 
   Platform SDK Web Install. I got it from this URL:
   http://www.microsoft.com/downloads/details.aspx?familyid=A55B6B43-E24F-4EA3-A93E-40C0EC4F68E5

   (Be careful not to download the x64 platform SDK unless you are
   running on a 64-bit machine!)

   - Be sure that you DO NOT chose the configuration option to
     Register environment variables. 

   YOU CANNOT COMPILE AFFLIB UNLESS THE PLATFORM SDK IS INSTALLED.

   - If possible, install the platform SDK as 
     C:\Program Files\Microsoft Platform SDK\. 

     If you can't do this, you will need to modify afflib.mak to
     reflect the actual install location

8. Finally, you must download and install OpenSSL:
   http://www.slproweb.com/products/Win32OpenSSL.html

   If you just want to run with openSSL, use this:
   http://www.slproweb.com/download/Win32OpenSSL_Light-0_9_8k.exe
   (In your win32/openssl directory)
   
   If you want to compile, use this:

   If you wish to compile, you will need to edit the file x509.h and
   add this line:

#ifdef OPENSSL_SYS_WIN32
/* Under Win32 these are defined in wincrypt.h */
#undef X509_NAME
#undef X509_CERT_PAIR
#undef X509_EXTENSIONS /* added by SLG */
#endif

   This is apparently a known bug in OpenSSL
   (http://wso2.org/forum/thread/3861), but it hasn't been fixed yet.


Compiling AFFLIB:
-----------------
1. Unpack the afflib distribution into a directory such as c:\afflib

2. From the Windows Start menu, run the Visual Studio 2005 Command Prompt

3. Change into the win32 subdirectory, e.g. "chdir c:\afflib\win32"

4. Type "make.bat" to run the makefile.

3. The command file "make.bat" will compile AFFLIB, LIBEWF, ZLIB and LZMA, and create a single .lib
   file. Compilation options are specified in afflib.mak in this directory.  

4. The following programs are ported:

   TARGETS = afcompare.exe afconvert.exe afcopy.exe afdiskprint.exe affix.exe afinfo.exe afstats.exe afxml.exe

   You can compile them all by typing:

   % make

   Alternatively, you can compile a single executable with:

   % make afcat.exe

5. To make the library alone:

* Open a VS2008 command prompt (run vcvars32.bat).
* Make sure you have OpenSSL and zlib installed.
* Add OpenSSL and zlib include paths to your INCLUDE path. 
* Run "make.bat afflib.lib" (inside the win32 directory).
* Rename afflib.lib to afflibMT.lib

Repeat 3 more times with different COMPILER_MODE to produce 3 more
libs:
========================================================================
* Run "make.bat clean" before each build, just to be safe.
* COMPILER_MODE /MD /O2 /D NDEBUG => rename to afflibMD.lib
* COMPILER_MODE /MTd => rename to afflibMTd.lib
* COMPILER_MODE /MDd => rename to afflibMDd.lib
* Put libs in world/3rdparty/afflib/VER/lib/vs20XX/win32

Repeat 4 more times to build 64-bit libraries with same names:
==============================================================
* Run vcvars64.bat instead of vcvars32.bat to set up environment.
* May want to reset INCLUDE path for OpenSSL and zlib, just to be
safe.
* Don't forget to use /O2 /D NDEBUG when building release (MT and MD).
* Note: No need to specify /MACHINE:X64 linker option in afflib.mak
because
  we're only building a static library, not linking to create any
  EXEs/DLLs.
* Run "make.bat clean" before each build, just to be safe.
* Put libs in world/3rdparty/afflib/VER/lib/vs20XX/x64


Once it is compiled:

1. To open a multi-file EnCase file, just specify the first .E01 file; the AFFLIB 
   implementation will automatically look for all of the other EnCase files.

2. Right now you should really use this library for READING AFF & E01
   files, rather than WRITING them.  Writing should work, but it's not
   very well tested on Windows. S3 is currently not supported on Windows. 

3. If you want to change the compile switches, feel free. They're in afflib.mak




================================================================



Compiling AFFLIB with Cygwin
============================
Cygwin is a Unix emulation system that allows standard Linux/Unix open
source software to be run on top of Windows through the use of a
special "cygwin" DLL. 

To use Cygwin, follow these step-by-step instructions:

1. Go to http://www.cygwin.com/. 

2. Click "Download Cygwin Now"; this will give you an executable file.

3. Run the Cygwin Net Release Setup Program. 

4. Select "Install from Internet"

5. Install into the C:\cygwin directory for All Users. Select
   "Unix/binary" as the default text file type.

6. Select a mirror site.

7. Click on the arrows next to "Devel" to change the word "Default" to
   "Install". This will cause the entire Cygwin development system to
   be installed.

8. Click "Next" and come back in an hour.

9. When you get the "Installation Complete" message, start the cygwin
   shell and type the following:

	 $ mkdir afflib
	 $ cd afflib
	 $ wget http://www.afflib.org/afflib.tar.gz
	 $ tar xfvz afflib.tar.gz
	 $ cd afflib*
	 $ ./configure
	 $ make
	 $ make install

  NOTE: EnCase support will NOT be compiled in unless you separately
  download and install LIBEWF.  libewf must be downloaded from 
  https://www.uitwisselplatform.nl/projects/libewf/
	 

10. You should now have a working system.



#
# Local Variables:
# mode: flyspell
# mode: auto-fill
# End:
# LocalWords: AFFLIB
#
