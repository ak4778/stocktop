include Makefile.opt

# Les variables globales sont définies dans le fichier Makefile.opt,
# mais on peut les surcharger ici :

# Pour les redéfinir on utilise '='
#   Exemple : IDL=mon_nouveau_compilateur_idl

# Pour les compléter on utilise '+='
#   Exemple : CC_FLAGS+=un_nouveau_flag_de_compilation

PACKAGE = Stock

LOG_FILE = $(STOCK_ROOT)/log

# Répertoires projet
DIR_PROC     := $(STOCK_ROOT)/proc/

DIR_INC      = $(STOCK_ROOT)/include/

DIR_PROC_OBJ := $(STOCK_ROOT)/proc/

DIR_TOP_OBJ  := $(STOCK_ROOT)/

DIR_EXE      := $(STOCK_ROOT)

DIR_LIB      := $(STOCK_ROOT)/lib/

INC_FLAGS    := -I$(DIR_INC) -I./ 

#CURSES := -lncurses

LIBS         := -lncurses -ldl

INSTALL_DIR  := /usr/lib

PKG_CPPFLAGS := -D_GNU_SOURCE -I proc
CPPFLAGS     := -I/usr/include/ncurses
ALL_CPPFLAGS := $(PKG_CPPFLAGS) $(CPPFLAGS) $(INC_FLAGS)

PKG_CFLAGS   := -fno-common -ffast-math \
  -W -Wall -Wshadow -Wcast-align -Wredundant-decls \
  -Wbad-function-cast -Wcast-qual -Wwrite-strings -Waggregate-return \
  -Wstrict-prototypes -Wmissing-prototypes
# Note that some stuff below is conditional on CFLAGS containing
# # an option that starts with "-g". (-g, -g2, -g3, -ggdb, etc.)
CFLAGS       := -O2 -s
CFLAGS       += -fPIC #modify

ALL_CFLAGS   := $(PKG_CFLAGS) $(CFLAGS)

PKG_LDFLAGS  := -Wl,-warn-common
LDFLAGS      := -Wl,-rpath $(STOCK_RPATH) #modify
ALL_LDFLAGS  := $(PKG_LDFLAGS) $(LDFLAGS)

############ Add some extra flags if gcc allows

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),tar)  
ifneq ($(MAKECMDGOALS),extratar)
ifneq ($(MAKECMDGOALS),beta)

# Unlike the kernel one, this check_gcc goes all the way to
# producing an executable. There might be a -m64 that works
# until you go looking for a 64-bit curses library.
check_gcc    := $(shell if $(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) dummy.c $(ALL_LDFLAGS) $(1) -o /dev/null $(CURSES) > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi ;)

# Be 64-bit if at all possible. In a cross-compiling situation, one may
# do "make m64=-m32 lib64=lib" to produce 32-bit executables. DO NOT
# attempt to use a 32-bit executable on a 64-bit kernel. Packagers MUST
# produce separate executables for ppc and ppc64, s390 and s390x,
# i386 and x86-64, mips and mips64, sparc and sparc64, and so on.
# Failure to do so will cause data corruption.
m64          := $(call check_gcc,-m64,$(call check_gcc,-mabi=64,))
ALL_CFLAGS   += $(m64)

ALL_CFLAGS   += $(call check_gcc,-Wdeclaration-after-statement,)
ALL_CFLAGS   += $(call check_gcc,-Wpadded,)
ALL_CFLAGS   += $(call check_gcc,-Wstrict-aliasing,)

# Adding -fno-gcse might be good for those files which
# use computed goto.
#ALL_CFLAGS += $(call check_gcc,-fno-gcse,)

# if not debugging, enable things that could confuse gdb
ifeq (,$(findstring -g,$(filter -g%,$(CFLAGS))))
ALL_CFLAGS   += $(call check_gcc,-fweb,)
ALL_CFLAGS   += $(call check_gcc,-frename-registers,)
ALL_CFLAGS   += $(call check_gcc,-fomit-frame-pointer,)
endif

# in case -O3 is enabled, avoid bloat
ALL_CFLAGS   += $(call check_gcc,-fno-inline-functions,)

endif
endif
endif
endif

# List Objets
OBJ_COMMUN   = $(DIR_PROC_OBJ)alloc.o       \
               $(DIR_PROC_OBJ)devname.o     \
               $(DIR_PROC_OBJ)escape.o      \
               $(DIR_PROC_OBJ)ksym.o        \
               $(DIR_PROC_OBJ)pwcache.o     \
               $(DIR_PROC_OBJ)readproc.o    \
               $(DIR_PROC_OBJ)sig.o         \
               $(DIR_PROC_OBJ)slab.o        \
               $(DIR_PROC_OBJ)sysinfo.o     \
               $(DIR_PROC_OBJ)version.o     \
               $(DIR_PROC_OBJ)whattime.o


OBJ_TOP      = $(DIR_TOP_OBJ)fileutils.o    \
               $(DIR_TOP_OBJ)top.o          \
               $(DIR_TOP_OBJ)top_nls.o      

OBJ_ALL      = $(OBJ_COMMUN)                \

OBJ_ALL_TEST = $(OBJ_TOP)                   \

DEP_ALL      = $(OBJ_ALL:.o=.d)             \
               $(OBJ_ALL_TEST:.o=.d)

SO_NAME      = scommon

LIB_FILE     = lib$(SO_NAME).so

LIB_SCOMMON  = $(DIR_LIB)$(LIB_FILE)

TOP_EXE      = $(DIR_EXE)/$(APP_NAME)

# Definition the cibles 
# ----------------------------------------------------------
.PHONY : all lib stock mrproper clean distclean init_dirs install_lib install_exe install 
all : 
	@$(MAKE) lib
	@$(MAKE) stock
	@echo "[STOCK] Construction terminated !"

init_dirs : 
	mkdir $(STOCK_RPATH) -p
	@echo "[STOCK] init_dirs : Nothing to do, all done..."

lib : init_dirs
	@$(MAKE) $(LIB_SCOMMON)
	@echo "[STOCK] Construction library libscommon OK"

stock :
	@$(MAKE) $(TOP_EXE)
	@echo "[STOCK] Construction stock terminated !"

mrproper : clean
	@echo "[STOCK] Suppression the files generated"
	@echo "[STOCK] Suppression the binaires construits"
	rm -f $(LIB_SCOMMON) $(TOP_EXE) 

clean :
	@echo "[STOCK] Suppression the objets of dependances"
	rm -rf $(DIR_PROC_OBJ)*.o
	rm -rf $(DIR_PROC_OBJ)*.d
	rm -rf $(DIR_LIB)*
	rm -rf $(LIB_SCOMMON)
	rm -rf $(STOCK_RPATH)/$(LIB_FILE)
	rm -rf $(INSTALL_DIR)/$(LIB_FILE)
	rm -rf $(OBJ_TOP)
	rm -rf $(TOP_EXE)
	rm -rf $(LOG_FILE)
	@echo "[STOCK] Suppression the objets of dependances terminated !"

distclean : mrproper
	@echo "[STOCK] distclean: Suppression the environnement cible"
	rm -f ${adacs_pfa}/library/lib_c/lib stock common*

install_lib : 
	@$(MAKE) lib
	sudo cp -f $(LIB_SCOMMON) $(INSTALL_DIR) 
	@echo "[STOCK] Installation de la library lib stock common OK"

install_exe :
	@echo "[STOCK] Pas installation executable (reste local)"

install : 
	@$(MAKE) install_lib
	@echo "[STOCK] Installation terminated"

# -----------------------------
$(TOP_EXE) : $(LIB_SCOMMON) $(OBJ_TOP)
	@echo "[STOCK] Création de $@"
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -o $@ $(OBJ_TOP) $(LIBS) -L$(STOCK_RPATH) -l$(SO_NAME) $(LDFLAGS)

$(LIB_SCOMMON) : $(OBJ_ALL)
	@echo "[STOCK] Création de $@"
	@$(CC) -g $(LDFLAGS) -shared -o $@ $(OBJ_ALL)

# Compilation the sources
# -----------------------

# Commande de compilation standard
#CC_COMPIL= $(CC) $(CCFLAGS) $(DFLAGS) $(INC_FLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -c -o "$@" "$<"

# Commande de compilation the objets de stock

# Integration the dependances (doit  figurer juste avant les regles implicites)
# -------------------------------------------------------------------------------------------
ifneq ($(strip $(DEP_ALL)),)
-include $(DEP_ALL)
endif

# Definition the regles implicites (doit  figurer en dernier)
# -------------------------------------------------------------------------
#
$(DIR_PROC_OBJ)%.o : $(DIR_PROC)%.c
	@echo "[proc] Compilation the $<"
	$(CC) $(DFLAGS) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

%.o : %.c
	@echo "[proc] Compilation the $<"
	$(CC) $(DFLAGS) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<
