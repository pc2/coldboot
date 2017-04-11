# Hardware Accelerated Cold-Boot Attacks

This repository contains different implementations of *AES key reconstruction*
as required in *cold-boot attacks*. We utilize custom FPGA-based designs in
order to accelerate the reconstruction in hardware and make use of *Work
Stealing* techniques as well as *Instance-Specific Hardware Designs* to address
particularly demanding problem instances.

For more information visit our project website at
https://pc2.uni-paderborn.de/coldboot and refer to our scientific publications
listed below.

## How to Use

### Software Implementation

The software implementation can be found in the directory `software`. It uses
*Intel Cilk Plus* to implement work stealing. We tested the compilation and
execution with *Intel C/C++ Compiler (ICC)* in version 17.0.2.

To build and run a demo application utilizing the software implementation,
simply run

    make demo-sw
    ./run_software.sh

The binary executable itself expects the following arguments:

    aeskeyfix_workstealing_cilk <total decay rate> <loop start iteration> <loop end iteration> <repetitions> <decay rate p1>
    # e.g. aeskeyfix_workstealing_cilk 0.3 0 1 1 0.001

This command reconstructs one key schedule (the first; loop range 0...1), which
has a total decay rate of 30% and a value of 0.1% for p1.

### Hardware Implementation

Our hardware implementation targets the *Maxeler MAX3A Vectis Dataflow Engine*
which features a *Xilinx Virtex 6 (XC6VSX475T)* FPGA. We use *MaxelerOS* and the
*MaxCompiler* in version 2014.2.

Assuming the Maxeler environment properly loaded, building and running a demo
application utilizing the hardware implementation works similar as the software
implementation showed above:

    make demo-hw
    ./run_hardware.sh

This approach uses prebuilt maxfiles, so no FPGA synthesis is required.

### Modifying the Hardware Implementation

In the directory `hardware` are two ready-to-use Maxeler projects available
which can be used as a template for modified designs.

#### Building (synthesising) the Hardware Design

Depending on your system, building the hardware design (synthesis) will take
several hours. This step generates the *maxfile*.

    cd Maxeler/RunRules/DFE
    make build

#### Building the Host Software

This step requires a previously synthesized design (the *maxfile*). The
resulting binary can be found in `Maxeler/RunRules/DFE/binaries`.

    cd Maxeler/CPUCode
    make RUNRULE=DFE build

Please note that the host code contains hardcoded paths for the input data. You
might need to adjust these paths before starting the compilation.

#### Changing the Guess Order

The order in which byte values are guessed during key reconstruction is read
from a binary file, containing 16*256 data entries of type uint8_t. Entries
0..255 define the order of values that are guessed for the first byte. Entries
256..511 define the order for the second guessed byte and so on.

When building the DFE design using our generator (see following section), a
heuristically chosen guess order for the corresponding key schedule will be
created automatically.

#### Generating Instance-Specific DFE Designs

To generate an instance-specific hardware design, build and run our generator as
follows:

    make generator
    generator/dfe_generator_isc <ID of decayed key schedule> <p1> <p0>
    # e.g. generator/dfe_generator_isc 0 0.001 0.299

Please note that the path to the key schedules is currently hardcoded in the
generator code which you might need to adjust before starting the compilation.

The generator is configurable using several `#define` statements. A
preconfigured generator for the general design is available as `dfe_generator`.
This can e.g. be used to generate optimized guess orders for the general
hardware design (see above).

## Publications

* H. Riebler, M. Lass, R. Mittendorf, T. Löcke, and C. Plessl  
  Efficient Branch and Bound on FPGAs using Work Stealing and Instance-Specific Designs  
  ACM Trans. on Reconfigurable Technology and Systems (TRETS). 2017. Accepted for publication.

* H. Riebler, T. Kenter, C. Plessl, and C. Sorge  
  Reconstructing AES Key Schedules from Decayed Memory with FPGAs  
  In Proc. Int. Symp. on Field-Programmable Custom Computing Machines (FCCM). Pages 222–229. IEEE Computer Society. Apr. 2014.

* H. Riebler, T. Kenter, C. Sorge, and C. Plessl  
  FPGA-accelerated Key Search for Cold-Boot Attacks against AES  
  In Proc. Int. Conf. on Field Programmable Technology (ICFPT). Pages 386–389. IEEE. Dec. 2013.
