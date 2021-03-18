* audio
  - [x] Add master clock and machine frequency
  - [x] Interface - add uint8_t audioBuffer[16]
  - [x] Interface::Interface() // add platform
    * if !XOCHIP, load 200Hz
    * if XOCHIP, load silence
  - [x] Interface::loadAudio(uint8_t buffer[16])
  - [x] Interface::startAudio()
    * reset audio bit index, mark to stream from buffer
  - [x] Interface::stopAudio()
    * mark to stream silence
  - [x] Set system clock as lowest common multiple of 4000Hz and CPU rate
  - [x] Pass Clock instead of clk_t
  - [x] clk_t {Chip8Interpreter,Interface}::calculateNextClock(Clock targetClock)
    * when does clock rise and state needs to be updated?
    * could be currentClock
    * this is an optimization; could just updateToClock by 1 each step
    * return (currentClock + clockRate - 1) / clockRate * clockRate;
  - [x] Interface - open libao, prepare buffer, determine buffer size
  - [x] Interface::updateToClock(Clock systemClock)
    - [x] increment through sample buffer or silence, emitting buffer when buffer is filled
  - [x] Chip8Interpreter::updateToClock(Clock systemClock)
    * remove DT and ST decrement from Tick - delete tick?
    * and Chip8Interpreter::clockRate // in instructions per second
    * decrement DT and ST if clock multiple of 60Hz
      * call stopAudio when ST becomes 0
  - [x] main()
    * while(!done)
      * clock = currentClock; while(clock < currentClock + 1ms)
        * call calculateNextClock on Chip8Interpreter and Interface, call the next closest one, set new clock
    * if 16ms wall clock time has elapsed, call Interface::iterate
  
* https://chip-8.github.io/extensions/#octo
  * Octo apparently has slightly different behavior than "pure" xo-chip?
* arrow keys
* integrate launcher (parsing JSON) into xochip.cpp, remove launcher
