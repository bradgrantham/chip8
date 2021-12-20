This is a work-in-progress CHIP8 emulator, including S-CHIP and XO-CHIP.

Please clone with recursive submodules.  (The nlohmann::json submodule git history is quite large (~250MB), so the module is set to shallow.)
```
    git clone --recurse this-repo
```

Alternatively, init the submodules after cloning:
```
    git submodule update --init --recursive --depth 1
```

Build with CMake:
```
    (mkdir -p build && cd build && cmake .. && make)
```

`xochip` is an executable that emulates CHIP8.  It accepts several command-line options to specify extension or "quirk" behavior.  Run `xochip -h` for a little more information.

`launcher` reads the JSON manifest of [CHIP8 titles from John Earnest's OctoJam](https://johnearnest.github.io/chip8Archive/) and creates an `xochip` command line that represents the appropriate extensions and quirks.

Run one ROM, e.g. chip8Archive's "snake", from bash:

```
    PATH=$PATH:`pwd`/build `build/launcher chip8Archive/programs.json chip8Archive/roms snake`
```

Press ESC to exit.

Run all ROMs from bash (you'll have to interrupt the bash command-line):
```
    cat roms.txt | while read name ; do echo $name 2>&1 ; PATH=$PATH:`pwd`/build `build/launcher ../chip8Archive/programs.json ../chip8Archive/roms $name` ; done
```

Alternatively, the script `RUN_ALL_ROMS` will run that command.

Many ROMs work properly at the moment; a few may exit due to an assert.  I use [John Earnest's OctoJam page of ROMs that run in the browser](https://johnearnest.github.io/chip8Archive/) as my reference.

This emulator supports features in [John Earnest's XO-Chip Specification](https://github.com/JohnEarnest/Octo/blob/gh-pages/docs/XO-ChipSpecification.md) through December 2020.  (I.e. the `pitch` and extended `saveflags` are not supported.)

This has been tested on MacOS 10.14, MacOS 10.15, and Debian 10 Linux.
