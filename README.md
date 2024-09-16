The text written in English is [here](README_en.md).
# Aoba振り飛車

「Aoba振り飛車」は、将棋の振り飛車を人間の知識なしでゼロから深層強化学習させるユーザ参加型の将棋人工知能プロジェクトです。  
対抗形の先手四間飛車、後手中飛車などに加えて、先手1間飛車、後手向飛車、などの相振りも学習します。
どの筋に飛車を振っても勝率が5割になるように強さを自動調節しています。
AIは振り飛車の新しい指し方を発見できるでしょうか？1間飛車や9間飛車はどんな指し方に？

知識ゼロ、と書きましたが、厳密には「振り飛車の定義」と「希望する筋に飛車を振れたことへのボーナス」を
与えています。詳しくは[こちらを](http://www.yss-aya.com/furibisha/furi.html)。

集めた棋譜や棋力のグラフ、棋譜のサンプルなどは[こちら](http://www.yss-aya.com/furibisha/)で公開しています。
「Aoba振り飛車」は以下のプロジェクトの後継でもあります。  
[AobaZero](http://www.yss-aya.com/aobazero/)  
[Aoba駒落ち](http://www.yss-aya.com/komaochi/)

GPUがあれば、より高速に棋譜を生成できます。  
CPUだと10倍から100倍遅くなりますが、将棋をプレイして楽しむことは可能です。  

# 棋譜の生成に協力してみたい
[Windows用の実行ファイル(64bit版のみです)](https://github.com/yssaya/furibisha/releases)

CPUだけのマシンは
```
aobafuribisha-103-w64-cpu-only.zip
```
GPUがついたマシンは
```
aobafuribisha-103-w64-opencl.zip
```
をダウンロード、展開して、中のclick_me.batを実行してください。  
GPUの種類によっては autousi.cfg の  
```
Device        O-1:3:7W 
```
を変更することでより高速で動作する場合があります。[autousi.cfg](autousi.cfg) の他のサンプルをご参考下さい。ただWindows版ではプロセス数を36以上に増やすと起動に失敗することがあります。  

Linuxの方は[readme.txt](readme.txt)の手順でコンパイルしてから
```
./bin/autousi
```
を実行してください。

# 将棋所で遊んでみたい
CPU版をダウンロードして、click_me.batを実行します。しばらくすると最新のネットワークの重みファイルをダウンロードして「self-play started」が表示されて棋譜の生成を開始します。すかさずCtrl + Cで停止させます。(signal 1 caught)が表示されて、しばらく待つと止まります。  
weight_save/の下にw000000000066.txt という20MBほどのファイルが作られます。
(66、の数値は異なります)

aobafuribisha-103-w64-cpu-only.zipに同梱されているaobaf.batを編集します。最後の1行が以下のようになっています。
```
bin\aobaf -q -i -p 100 -w weight-save\w000000000066.txt
```
この66の部分を実際にダウンロードしてきたファイル名に合わせて書き直し、保存します。
将棋所にaobaf.batをエンジンとして登録します。  
"-p 100"の100を増やすと強くなりますが、思考時間が長くなります。
CPU版は100で5秒ほどかかります。GPU版は4000で3秒ほどかかります(GPUの性能に依存します)。

将棋所はusiエンジンを動作させる将棋用のGUIです。こちらで入手できます。  
将棋所のページ  
<http://shogidokoro.starfree.jp/>

# ShogiGUIで遊んでみたい
Aoba振り飛車はShogiGUIでも動作します。「ツール(T)」「オプション(O)」で「棋譜解析、検討モードで指し手をすべて送る」にチェックを入れて下さい。  
チェックがない状態だと現在局面のみ(position sfen ... で move なし)なので、千日手の打開が若干甘くなります。

ShogiGUIのページ  
<http://shogigui.siganus.com/>

# ShogiHomeで遊んでみたい
ShogiHome(旧ElectronShogi)のページ  
<https://sunfish-shogi.github.io/electron-shogi/>

# コンパイルの仕方
[こちら](compile.txt)をご覧ください。

# Aoba振り飛車の紹介ページ
今までに作成した棋譜や重み、棋譜のサンプルなどを公開しています。  
<http://www.yss-aya.com/furibisha/>

# License
usiエンジンであるaobakはGPL v3です。それ以外はpublic domainです。  
[短めのライセンス](license.txt)  
詳しくは[こちら](https://github.com/yssaya/furibisha/tree/master/licenses)をご覧ください。

# Link
 - [AobaZero](https://github.com/kobanium/aobazero)
 - [Aoba駒落ち](https://github.com/yssaya/komaochi)
 - [Leela Zero (Go)](https://github.com/leela-zero/leela-zero)
 - [LCZero (Chess)](https://github.com/LeelaChessZero/lczero)

