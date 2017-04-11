ifndef MAXCOMPILERDIR
  $(error The environment variable MAXCOMPILERDIR is not defined)
endif

all: 

build: Makefile.rules
	@java -jar "$(MAXCOMPILERDIR)/lib/MaxIDE/projectAdmin.jar" -u ../.. DFE
	$(MAKE) -f Makefile.rules build

installer_no_dependencies: Makefile.rules
	@java -jar "$(MAXCOMPILERDIR)/lib/MaxIDE/projectAdmin.jar" -u ../.. DFE
	$(MAKE) -f Makefile.rules installer_no_dependencies

maxfiles/%.max include/%.h: Makefile.rules
	@java -jar "$(MAXCOMPILERDIR)/lib/MaxIDE/projectAdmin.jar" -u ../.. DFE
	$(MAKE) -f Makefile.rules $@

clean distclean startsim stopsim: Makefile.rules
	$(MAKE) -f Makefile.rules $@

Makefile.rules: Makefile.settings

Makefile.settings: RunRules.settings
	java -jar "$(MAXCOMPILERDIR)/lib/MaxIDE/projectAdmin.jar" -u ../.. DFE

.PHONY: all build clean installer_no_dependencies
