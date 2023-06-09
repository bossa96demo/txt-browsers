.IFDEF MMSVAX
.IFDEF EXTERNAL_SSL
.ELSE
NO_SSL=1
.ENDIF
CFLAGS_ARCH=/PRECISION=SINGLE/ASSUME=(NOWRITABLE_STRING_LITERALS)/LIST/MACHINE_CODE=BEFORE
.ELSE
CFLAGS_ARCH=/FLOAT=IEEE/IEEE_MODE=UNDERFLOW_TO_ZERO/PRECISION=SINGLE/ASSUME=(NOACCURACY_SENSITIVE,NOMATH_ERRNO,WEAK_VOLATILE,NOWRITABLE_STRING_LITERALS)/WARNINGS=(DISABLE=NOMAINUFLO)/LIST/MACHINE_CODE
.ENDIF

.IFDEF DEBUG
CFLAGS_OPTIMIZE=/DEBUG/NOOPTIMIZE
DEFS_OPTIMIZE=,VMS_DEBUGLEVEL=2
.ELSE
.IFDEF MMSVAX
CFLAGS_OPTIMIZE=/OPTIMIZE
.ELSE
CFLAGS_OPTIMIZE=/OPTIMIZE=(LEVEL=5,TUNE=GENERIC)
.ENDIF
DEFS_OPTIMIZE=
.ENDIF

.IFDEF 64BIT
CFLAGS_64=/POINTER_SIZE=64
.ELSE
CFLAGS_64=
.ENDIF

.IFDEF NO_SSL
DEFS_SSL=
INCLUDE_SSL=
LIBS_SSL=
.ELSE
DEFS_SSL=,HAVE_SSL,HAVE_OPENSSL
INCLUDE_SSL=
.IFDEF 64BIT
.IFDEF EXTERNAL_SSL
.IFDEF MMSALPHA
LIBS_SSL=,VMS-SSL-EXTERNAL-ALPHA-64/OPT
.ELSE
LIBS_SSL=,VMS-SSL-EXTERNAL-I64-64/OPT
.ENDIF
.ELSE
LIBS_SSL=,VMS-SSL-64/OPT
.ENDIF
.ELSE
.IFDEF EXTERNAL_SSL
.IFDEF MMSVAX
LIBS_SSL=,VMS-SSL-EXTERNAL-VAX-32/OPT
.ELSE
.IFDEF MMSALPHA
LIBS_SSL=,VMS-SSL-EXTERNAL-ALPHA-32/OPT
.ELSE
LIBS_SSL=,VMS-SSL-EXTERNAL-I64-32/OPT
.ENDIF
.ENDIF
.ELSE
LIBS_SSL=,VMS-SSL-32/OPT
.ENDIF
.ENDIF
.ENDIF

.IFDEF EXTERNAL_GFX
DEFS_GFX=,HAVE_ZLIB,G,HAVE_PNG_H,HAVE_PNG_SET_RGB_TO_GRAY,HAVE_PNG_GET_LIBPNG_VER,HAVE_JPEG,GRDRV_X,HAVE_X11_XLOCALE_H,HAVE_XSETLOCALE,X_INPUT_METHOD
.IFDEF 64BIT
LIBS_GFX=,VMS-GFX-64/OPT
.ELSE
LIBS_GFX=,VMS-GFX-32/OPT
.ENDIF
.ELSE
DEFS_GFX=
LIBS_GFX=
.ENDIF

CFLAGS=/DEFINE=(HAVE_CONFIG_VMS_H$(DEFS_OPTIMIZE)$(DEFS_SSL)$(DEFS_GFX)) /INCLUDE_DIRECTORY=(GNU:[INCLUDE]$(INCLUDE_SSL)) $(CFLAGS_ARCH) $(CFLAGS_OPTIMIZE) $(CFLAGS_64) $(CFLAGS_EXTRA)

LINKFLAGS=$(LIBS_SSL)$(LIBS_GFX)/MAP/FULL/THREADS_ENABLE

OBJS=AF_UNIX.OBJ,AUTH.OBJ,AVIF.OBJ,BEOS.OBJ,BFU.OBJ,BLOCK.OBJ,BOOKMARK.OBJ,CACHE.OBJ,CHARSETS.OBJ,COMPRESS.OBJ,CONNECT.OBJ,COOKIES.OBJ,DATA.OBJ,DEFAULT.OBJ,DIP.OBJ,DIRECTFB.OBJ,DITHER.OBJ,DNS.OBJ,DOH.OBJ,DOS.OBJ,DRIVERS.OBJ,ERROR.OBJ,FILE.OBJ,FINGER.OBJ,FN_IMPL.OBJ,FONT_INC.OBJ,FRAMEBUF.OBJ,FTP.OBJ,GIF.OBJ,HPUX.OBJ,HTML.OBJ,HTML_GR.OBJ,HTML_R.OBJ,HTML_TBL.OBJ,HTTP.OBJ,HTTPS.OBJ,IMG.OBJ,IMGCACHE.OBJ,JPEG.OBJ,JSINT.OBJ,KBD.OBJ,LANGUAGE.OBJ,LISTEDIT.OBJ,LRU.OBJ,MAILTO.OBJ,MAIN.OBJ,MEMORY.OBJ,MENU.OBJ,OBJREQ.OBJ,OS_DEP.OBJ,PMSHELL.OBJ,PNG.OBJ,SCHED.OBJ,SELECT.OBJ,SESSION.OBJ,SMB.OBJ,STRING.OBJ,SUFFIX.OBJ,SVG.OBJ,SVGALIB.OBJ,TERMINAL.OBJ,TIFF.OBJ,TYPES.OBJ,URL.OBJ,VIEW.OBJ,VIEW_GR.OBJ,VMS.OBJ,WEBP.OBJ,X.OBJ,XBM.OBJ

LINKS.EXE : $(OBJS)
	$(LINK) /EXECUTABLE=LINKS.EXE $(OBJS)$(LINKFLAGS)

CLEAN :
	- DELETE /NOCONFIRM /NOLOG *.OBJ;*
	- DELETE /NOCONFIRM /NOLOG *.EXE;*
	- DELETE /NOCONFIRM /NOLOG *.LIS;*
	- DELETE /NOCONFIRM /NOLOG *.MAP;*
