.PHONY: all demo-hw demo-sw generator

all: demo-hw demo-sw generator

demo-hw:
	make -C hardware/general/CPUCode RUNRULE=DFE
	make -C hardware/isc-0/CPUCode RUNRULE=DFE

demo-sw:
	make -C software

generator:
	make -C generator
