# koe

AI エージェントが人間へ声で合図するための小さな CLI。

```console
$ koe "3秒後にボタンを押してください"
```

VOICEVOX CORE でオフライン合成し、その場で鳴らして、喋り終わるまで返らない。
ネット接続も API キーも要らない。設計は [docs/DESIGN.md](docs/DESIGN.md) を参照。

## 対応環境

| OS | 再生 | 状態 |
| --- | --- | --- |
| Linux x64 | PulseAudio / PipeWire / ALSA | 動作確認済み |
| Windows x64 | WASAPI | CI でビルドのみ確認。再生は未検証 |
| macOS (arm64) | CoreAudio | CI でビルドのみ確認。再生は未検証 |

## Install

### 1. VOICEVOX CORE の配置

公式 Downloader で C API・ONNX Runtime・Open JTalk 辞書・音声モデルを一箇所に置く。
CPU 版で足りる。

```console
$ curl -sSfL https://github.com/VOICEVOX/voicevox_core/releases/latest/download/download-linux-x64 -o download
$ chmod +x download
$ ./download -o ~/.local/share/koe/voicevox_core --models-pattern '0.vvm'
```

Windows は PowerShell で `download-windows-x64.exe` を取得して同様に実行する。
`--models-pattern` は初期話者（ずんだもん・四国めたん）の入った `0.vvm` だけを取る指定で、
他の話者が欲しければ外す。Downloader は音声モデルと ONNX Runtime の利用規約への同意を
対話で求める（`y` で同意）。配置は合計 180MB ほど。

音声モデルには [利用規約](https://github.com/VOICEVOX/voicevox_vvm/blob/main/TERMS.txt) がある。
生成した音声を公開する場合は VOICEVOX と話者のクレジット表記が要る。

### 2. 実行ファイルの配置

[Releases](https://github.com/neguse/koe/releases) から OS に合うものを取り、
`koe`（Windows は `koe.exe`）という名前で PATH に置く。

```console
$ curl -sSfL https://github.com/neguse/koe/releases/latest/download/koe-linux-x64 -o ~/bin/koe
$ chmod +x ~/bin/koe
```

手元でビルドするなら Linux / macOS は `make`（C コンパイラのみ必要）、Windows は
`.github/workflows/build.yml` の `cl` と `mt.exe` の行をそのまま実行する。
`miniaudio.h` と `voicevox_core.h` はリポジトリに同梱している。

### 3. 設定

`~/.config/koe/config`（Windows は `%APPDATA%\koe\config`）に `key=value` を書く。
無ければ既定値で動く。

```ini
# VOICEVOX CORE の配置先（Downloader の -o に渡した場所）
core=/home/neguse/.local/share/koe/voicevox_core
# スタイル ID。3 はずんだもん・ノーマル
style=3
# 出力デバイスの ID。空なら OS の既定。候補は `koe devices` で出る
# （Linux では `pactl list short sinks` の sink 名と同じ）
device=
# 音量倍率
volume=1.0
```

## Usage

```console
$ koe "文"                 # 引数の文を喋る
$ echo "文" | koe          # 標準入力からも受ける
$ koe devices              # 出力デバイスの一覧（ID<TAB>名前、既定に * ）
$ koe styles               # 配置済みモデルの話者とスタイル ID（JSON）
$ koe -o out.wav "文"      # 鳴らさずに WAV をファイルに書く
$ koe -s 1.15 "文"         # 話速を変える（VOICEVOX の speedScale、1 が標準）
```

終了コードは、喋り終えたら 0、合成や再生に失敗したら 1、使い方の誤りは 2。
一文あたり、初期化に 1 秒弱、合成に 1〜2 秒かかってから喋り始める。

VOICEVOX CORE のログは `RUST_LOG` で制御する。未設定なら `error` 相当に絞る。
