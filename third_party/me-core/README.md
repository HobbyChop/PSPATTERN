## PSP Media Engine Custom Core: Mapper Library.
A library project to map the PSP's Media Engine core functions with the purpose of using them in our code.  
The main idea is simply to make the native kernel/core functions of the media engine available to homebrew developers.  

Additionally, the library comes with some custom functions and processes to ease the use and integration of the Media Engine into homebrew projects.
It provides a Media Engine Core Mapper (meCoreMapper), a Media Engine Custom Library (meLib) and a Virtual Mobile Engine Custom Library (vmeLib).

The Media Engine Core is loaded at address 0x88300000, however depending on the device model, it is loaded from a different image which means that addresses are not the same for models prior to the slim.
As a solution, we have several static correspondence tables and the code will select the table corresponding to our device before launching the Media Engine.

**Note:** This is a work in progress, and the mapping table for newer device generations is evolving faster than for older ones.

### More precisely, what does this provide?
+ Its primary goal is to provide access to some of the 'native' ME core functions by dynamically switching to the right table mapping according to the device model
+ It is provided as a static library: the core header (me-core.h) must be included once for initialization, while the me-lib headers can be included anywhere to access core and utility functions
+ It does not depend on the ME wrapper
+ It embeds a minimalistic PRX providing kernel callbacks and patching sceMeRpc on the main CPU side
+ It puts the VME in an optimized state, giving the ME CPU core additional resources
+ It implements a custom library providing access to the VME capabilities
+ It implements a custom library for DMACPLUS usage
+ It redefines the top of the local stack according to the device model, which provides larger local available eDRAM to work with
+ It removes main RAM and hardware register access restrictions
+ It manages suspend events (sleep/wake) callback handlers
+ It provides exception callbacks such as for general exceptions or external soft interrupts
+ It has timer interrupt callback handling

### What's coming next
* Core mapping functions are continuously being identified and added
* Addition of some of the previously useful functions written during the Media Engine Reload project in 2024-25, like DMAC related functions for example
* Thread management is a work in progress, based on timer interrupt

## Usage

### modules
- me-core, me-core-custom
- me-core-mapper
- me-lib
- vme-lib
- dmacplus.h

### Basic Integration

Include the me-core-mapper in your project as follows:

**CMakeLists.txt:**
```cmake
target_link_libraries(project
  me-core
)
```

**Note:** Make sure your project declaration includes C language support:
```cmake
project(your-project-name C CXX ASM)
```

**Alternatively using a Makefile:**
```makefile
LIBS += lme-core
```

**Code example:**
```cpp
#include <me-core-mapper/me-core.h>

void meLibOnProcess() {
  // ... your ME code
}

int main() {
  const int tableId = meLibDefaultInit();
  // ... main code
  return 0;
}
```

**Using the optional Exception Handlers:**
```cpp
#include <me-core-mapper/me-core.h>

extern "C" void meLibOnExternalInterrupt(void) {
  // if using meCoreEmitSoftwareInterrupt from sc
}

extern "C" void meLibOnException(void) {
  // optional, handle other custom exceptions here
}

void meLibOnProcess() {
  meLibExceptionHandlerInit(); // init exception handlers
  // ... your ME code
}

int main() {
  const int tableId = meLibDefaultInit();
  // ... main code
  return 0;
}
```

#### General Integration Steps:
1. Configure your build system to include the required directories
2. Include the `me-core-mapper/me-core.h` library header in your code
3. Implement `meLibOnProcess()` with your Media Engine code
4. Initialize the library in `main()` with `meLibDefaultInit()`

See related samples available in the `samples` folder for more information.

### Generate me-core-mapping.h

```bash
awk -f convert-mapping.awk me-core-mapping.def.h > me-core-mapping.c
```

### Generated Library with the embedded Prx
To build and install the current projet run:
```bash
make clean; make install;
```

This will copy the .a file to your pspdev/psp/lib folder and the related headers to the pspdev/psp/include folder. It embeds the small PRX used as a kernel bridge in a dedicated data section. At runtime, the library will extract the PRX next to the EBOOT and then load it.

## Contribution Guidelines

### AI-assisted development

AI tools may be used as development aids. However, the following rules apply strictly:

* All commits must be authored by a human contributor (pseudonyms are perfectly acceptable).
* The commit history must not contain any AI attribution as author or co-author.
* Contributors must fully review, understand, and validate all submitted code before opening a pull request.
* Contributors are expected to be able to explain and justify their changes during code review.
* The contributor is responsible for ensuring their code does not break existing functionality, including dependencies and the overall library behavior.

*In short: AI can assist, but humans must retain full ownership of the work.*

Pull requests that include AI attribution in commits, or that are not clearly understood and validated by the contributor, will be rejected.

### License compatibility

All code submitted to this repository must be compatible with the MIT License. Dependencies or code snippets under more restrictive licenses (e.g. GPL, LGPL, proprietary) are not accepted. Contributors are responsible for verifying that any third-party code they include is under a permissive license granting at least the same level of freedom as MIT.

## Disclaimer
This project and code are provided as-is without warranty. Users assume full responsibility for any implementation or consequences. Use at your own discretion and risk

## Related work
[PSP Media Engine Cracking The Unknown](https://github.com/mcidclan/psp-media-engine-cracking-the-unknown)  
[PSP Virtual Mobile Engine Extension](https://github.com/mcidclan/psp-virtual-mobile-engine-ext)  
[PSP Media Engine Safe Task](https://github.com/mcidclan/psp-media-engine-safe-task)  
[PSP Media Engine Reload](https://github.com/mcidclan/psp-media-engine-reload)  

## Thanks
Thanks to:
- crazyc, hlide and other ps2dev members for being pioneers on that subject
- CelestBlue, artart78, zecoxao, davee, xerpi and John (Hedge) for all the information available on psdevwiki and for reversing things
- Kotcrab for reversing things and for Ghidra scripts
- z2442 for the idea!
- Acid_Snake for testing
- All contributors to the PSP Hardware Registers page + the wiki
- Anyone else who contributed through documentation or reverse engineering

*m-c/d*
