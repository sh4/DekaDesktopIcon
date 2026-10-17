# DekaDesktopIcon

Explorer のデスクトップに 64～1024 px のアイコンを描画する Windows x64 用ツールです。DLL を実行中の Explorer に読み込み、実際のデスクトップ ListView の描画・座標処理をカスタマイズします。独立したランチャーやデスクトップの置き換えではありません。

デスクトップ上の項目、選択、ドラッグ、並べ替え、自動整列を使用できます。コンテキストメニューは既存の Explorer ビューに表示を委譲するため、Windows 11 の新形式／従来形式の設定に従います。

## ビルド

Windows x64、Visual Studio の「C++ によるデスクトップ開発」と Windows SDK が必要です。PowerShell から実行します。

```powershell
.\build.ps1
.\build\bin\DekaDesktopIcon.exe
```

生成物は次のとおりです。同じフォルダーの EXE と DLL を組み合わせて使用してください。

| ファイル | 用途 |
| --- | --- |
| DekaDesktopIcon.exe | 設定画面・コマンドライン操作 |
| DekaDesktopIconCore.dll | Explorer 内で動く描画・入力処理 |
| DekaDesktopIconTestHost.exe | 開発用のネイティブ ListView ホスト |
| DekaDesktopIconSortTest.exe | 開発用の Explorer 並べ替え検証 |

`build.ps1 -Configuration Debug`、`-OutputDirectory <フォルダー>` も指定できます。CMake を使う場合は、インストール済み Visual Studio のジェネレーターを指定します。

```powershell
cmake -S . -B build/cmake -G "Visual Studio 17 2022" -A x64
cmake --build build/cmake --config Release
```

CMake の実行ファイルと DLL は `build/cmake/bin` に生成します。バイナリ・中間生成物・検証画像・検証結果は Git の管理対象から除外しています。検証用 PNG はスクリプトが生成します。

## 使い方

1. `DekaDesktopIcon.exe` を起動し、表示サイズを選んで「Explorer に適用」を押します。
2. 初期状態では 30 秒後に自動解除します。継続利用する場合は試用のチェックを外して適用します。
3. 高解像度画像を使う場合は項目を選択し、「画像を選択…」「画像の割り当てを保存」で登録して再適用します。
4. 「解除・元の配置へ復元」で標準の描画・間隔・配置に戻します。

設定画面を閉じてもパッチは Explorer 内で動作します。Explorer を再起動した場合は再適用が必要です。システムファイル、レジストリ、自動起動設定は変更しません。

PNG / JPEG / BMP / ICO 等、Windows Imaging Component が読める画像に対応します。鮮明な 1024 px 表示には 1024 px 以上の画像を使ってください。標準アイコンの拡大では原画以上の細部は得られません。登録した画像ファイルは元の場所に保持してください。

背景の「表示」メニューから自動整列・等間隔整列を切り替えられます。適用中に変更した整列設定は解除後も維持します。整列が有効な場合、復元した位置が Windows により調整されることがあります。

### 設定と更新

既定の設定は `%LOCALAPPDATA%\DekaDesktopIcon\profile.ini` です。画像設定の直前の内容は `.bak`、適用前の配置は `.rollback.ini`、診断情報は `.diagnostic.ini` に保存します。Explorer 再起動後の配置復旧には「保存済みの元の配置を復旧」を使用できます。

改名前のツールを使用している場合は、旧版のコントローラーで解除してから DekaDesktopIcon を適用してください。旧版のパッチが有効な場合は重ねて適用することを拒否します。新しい設定ファイルがまだない場合に限り、旧 `%LOCALAPPDATA%\ExplorerIconPatch\profile.ini` の画像割り当てをコピーします。

解除後も DLL は Explorer に読み込まれたままです。更新時は解除して別のフォルダーへビルドしてください。読み込み中の DLL を上書きできるのは Explorer 終了後です。

### コマンドライン

```powershell
.\build\bin\DekaDesktopIcon.exe --apply --size 1024 --lease 30 --report build\apply.json
.\build\bin\DekaDesktopIcon.exe --status --report build\status.json
.\build\bin\DekaDesktopIcon.exe --restore --report build\restore.json
```

`--lease 0` は時間制限なし、`--profile <INI>` は設定ファイルの指定です。サイズは 64～1024、16 px 刻みです。GUI では 256 / 512 / 768 / 1024 を選択できます。

## 開発と検証

C++17 を使用し、`if` / `else` / `for` / `while` 等では一文でも中括弧を必須にしています。`.clang-format` と `.clang-tidy` に規則を定義しています。Visual Studio 付属または PATH 上の clang-format / clang-tidy 22 以降を使用します。

```powershell
.\scripts\Format-Source.ps1
.\scripts\Test-SourceStyle.ps1
.\validate.ps1
.\validate-safety.ps1
.\validate-sort.ps1
.\validate-interaction.ps1
.\validate-shell-behavior.ps1
```

実際の Explorer を操作する回帰試験は、動作中のパッチを解除してから順に実行してください。項目の選択・移動・整列設定・メニュー表示を操作し、終了時に復元します。検証中はデスクトップの操作を控えてください。

```powershell
.\validate-explorer.ps1
.\validate-explorer-sort.ps1
.\validate-interaction.ps1 -Explorer
.\validate-shell-behavior.ps1 -Explorer
```

各スクリプトの `-BinaryDirectory` で CMake 等の出力先を指定できます。結果は `build/validation` に出力します。操作・整列試験は 512 / 1024 px、並べ替え試験は画像リストの交換・縮小と実際の Shell コマンドを確認します。検証用の設定は通常利用の画像設定と分離しています。

構成・スレッド・復元処理については [技術メモ](docs/TECHNICAL.ja.md)、変更点は [CHANGELOG](CHANGELOG.md)、変更時の規則は [CONTRIBUTING](docs/CONTRIBUTING.md) を参照してください。

## 対応範囲

Windows 11 の Explorer が持つ `SHELLDLL_DefView` / `SysListView32` を対象とした非公式の実行時パッチです。全 Windows ビルドの互換性は保証しません。x64、同じログインセッション、最大 512 項目に対応します。管理者としての実行や追加の Visual C++ ランタイムは不要です。

1024 px のアイコンが複数ある場合は画面外にはみ出すことがあります。デスクトップ外へのドロップ等は Explorer の標準ドラッグ処理へ委譲します。混在 DPI、仮想デスクトップ、他のシェル拡張との組み合わせは未検証です。

## ライセンス

[MIT License](LICENSE)。著作権者は sh4 です。本プロジェクトの作成・リファクタリングには OpenAI Codex を使用しています。
