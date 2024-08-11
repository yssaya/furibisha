# AobaFuribisha
[AobaFuribisha](http://www.yss-aya.com/furibisha/index_e.html) is a distributed Deep reinforcement learning for
 Shogi Ranging Rook without human knowledge.  
Handicaps are seven kinds. Lance(kyo ochi), Bishop(kaku ochi), Rook(hisha ochi), 2-Piece(ni-mai ochi), 4-Piece(yon-mai ochi), 6-Pieces(roku-mai ochi) and No handicap(hirate).  
Winrate are adjusted to keep 0.5 by weakening Black(shitate or sente) player strength.  
Can AI discover a new opening, or rediscover Two-Pawn Sacrifice Push, Silver Tandem, etc?  

If you are interested, please join us. Anyone can contribute using Google [Colab](http://www.yss-aya.com/furibisha/colab_e.html).

# I'd like to cooperate with the generation of the game records.
[Executable file for Windows(only 64-bit version)](https://github.com/yssaya/furibisha/releases)

For machine without GPU
```
AobaFuribisha-1.0-w64-cpu-only.zip
```
For machine with GPU
```
AobaFuribisha-1.0-w64-opencl.zip
```
Download it, unzip, and run click_me.bat.

For Linux,
```
furibisha-1.0.tar.gz
```
Unzip it, make, then run
```
./bin/autousi
```
Please see [compile.txt](compile.txt) for details.

# I'd like to play with ShogiDokoro.
Download CPU version and run click_me.bat.
After a while, it downloads the latest network weight file, and "self-play started" is displayed, and self-play starts. Input "Ctrl + C" immediately. (signal 1 caught) is displayed and it will stop after a while.

weight_save/w0000000000066.txt will be created. Its size is about 162MB.
(the numbers "66" will be different.)

Edit aobaf.bat that is in AobaFuribisha-1.0-w64-cpu-only.zip.
The last line is like this,
```
bin/aobaf -q -i -p 100 -w weight-save\w000000000066.txt
```
Rewrite this "66" according to the file name actually downloaded, and save.
Register aobaz.bat as a engine in ShogiDokoro.
Increase by 100 of "-p 100", to get stronger. But it gets slower.
The CPU version takes about 5 seconds at 100. 
The GPU version takes about 3 seconds at 4000.(It depends on GPU.) 

ShogiDokoro is a GUI for USI engine.

ShogiDokoro 
<http://shogidokoro.starfree.jp/>

# I'd like to play with ShogiGUI
You have to change this option.  
[Tools(T)], [Options(O)], and Check [Send all moves].  

ShogiGUI 
<http://shogigui.siganus.com/>

# AobaFuribisha introduction page
There are game records, network weights, ELO progress and some self-play game samples.  
<http://www.yss-aya.com/furibisha/index_e.html>

# ShogiHome
ShogiHome(previous name, ElectronShogi)
<https://sunfish-shogi.github.io/electron-shogi/>

# License
USI engine aobak belongs to GPL v3. Others are in the public domain.
Short [license](license.txt).  
Detail is in the licenses in AobaFuribisha-1.0.tar.gz.

# Link
 - [AobaZero](https://github.com/kobanium/aobazero)
 - [AobaKomaochi](https://github.com/kobanium/komaochi)
 - [Leela Zero (Go)](https://github.com/leela-zero/leela-zero)
 - [LCZero (Chess)](https://github.com/LeelaChessZero/lczero)
