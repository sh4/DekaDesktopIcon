# DekaDesktopIcon

Explorer のデスクトップアイコンを画面に 2～3 個程度収まる大きさへ自動拡大する Windows x64 用ツールです。DLL を実行中の Explorer に読み込み、実際のデスクトップ ListView の描画・座標処理をカスタマイズします。

デスクトップ上の項目、選択、ドラッグ、並べ替え、自動整列を使用できます。コンテキストメニューは既存の Explorer ビューに表示を委譲するため、Windows 11 の新形式／従来形式の設定に従います。

ドラッグ中にフォルダーやごみ箱などへ重ねると、表示領域全体でドロップ先を判定し、対象の強調表示と移動／コピーなどの操作ヒントを表示します。操作内容とドロップの可否は Explorer が決定します。

## ビルド

Windows x64、Visual Studio の「C++ によるデスクトップ開発」、Windows SDK、.NET 10 SDK が必要です。WinUI 3 設定画面の初回ビルドでは NuGet への接続が必要です。PowerShell から実行します。

```powershell
.\build.ps1
.\build\bin\DekaDesktopIcon.exe
```

生成物は次のとおりです。EXE・DLL と `Settings` フォルダーをまとめて配置してください。設定画面の実行には **.NET 10 Runtime（x64）** が必要です。WinUI に必要な Windows App SDK ライブラリを同梱します。

| ファイル | 用途 |
| --- | --- |
| DekaDesktopIcon.exe | トレイ常駐・コマンドライン操作 |
| DekaDesktopIconCore.dll | Explorer 内で動く描画・入力処理 |
| Settings/DekaDesktopIcon.Settings.exe | WinUI 3 の設定画面 |
| DekaDesktopIconTestHost.exe | 開発用のネイティブ ListView ホスト |
| DekaDesktopIconSortTest.exe | 開発用の Explorer 並べ替え検証 |

`build.ps1 -Configuration Debug`、`-OutputDirectory <フォルダー>` も指定できます。コアの開発・回帰試験だけを行う場合は `-NativeOnly` で設定画面のビルドを省略できます。CMake を使う場合は、インストール済み Visual Studio のジェネレーターを指定します。

設定画面は `SelfContained=false` で発行し、実行環境の .NET 10 を使用します。SDK の参照は WinUI コンポーネントへ絞っています。再ビルド時は以前の設定画面の出力フォルダーを置き換え、古い .NET ランタイムや使わなくなった依存ファイルが残ることを防ぎます。

```powershell
cmake -S . -B build/cmake -G "Visual Studio 17 2022" -A x64
cmake --build build/cmake --config Release
```

CMake の実行ファイルと DLL は `build/cmake/bin` に生成します。バイナリ・中間生成物・検証画像・検証結果は Git の管理対象から除外しています。検証用 PNG はスクリプトが生成します。

GitHub Actions では **Build release binaries** を管理者が手動実行すると、Windows x64 の配布用 ZIP と SHA-256 をビルドし、バージョンタグに対応する GitHub Release へ自動登録します。実行方法と GitHub 側の保護設定は [リリース手順](docs/RELEASING.md) を参照してください。

## 使い方

1. `DekaDesktopIcon.exe` を起動すると、設定画面を表示せず、自動サイズで拡大してタスクトレイへ常駐します。初期状態に時間制限はありません。
2. トレイアイコンを左クリックすると WinUI 3 の設定画面を開きます。自動／手動サイズ、30 秒の試用、画像割り当て、状態、保存済み配置の復旧を操作できます。
3. トレイアイコンを右クリックすると、設定、有効、無効、終了を選べます。無効にすると標準の描画・間隔・配置に戻し、常駐は続けます。
4. 設定画面を閉じても常駐を続けます。「終了」は拡大を解除し、元の配置を復元してから Explorer 内の DLL をアンロードし、アプリを終了します。無効化・試用期限後も DLL をアンロードし、再び有効にすると読み込み直します。

常駐中は Explorer の再起動後も拡大を再適用し、通知領域の再作成時にトレイアイコンを再登録します。30 秒の試用が終了した場合は再適用せず無効のままにします。システムファイル、レジストリ、自動起動設定は変更しません。

マルチディスプレイでは作業領域の面積が最も小さいディスプレイを基準にします。自動モードは、ラベルや間隔も含めて画面内へ約 2～3 個収まるサイズを 16 px 刻み・64～4096 px で計算します。解像度・縦横・作業領域の変更時にも再計算します。項目数自体を制限する機能ではなく、残りの項目は画面外へ並ぶことがあります。

手動モードは適用時の最小ディスプレイのサイズを基準に、縦横の解像度比の小さい方に合わせて 64～1024 px で調整します。たとえば 3840 × 2160 で 1024 px を適用後、1920 × 1080 に変わると 512 px になります。元の解像度に戻ると指定サイズへ戻り、再適用するとその時点の解像度が新しい基準になります。

画像を割り当てていない項目は、Explorer の Shell が提供するサムネイルを優先して拡大表示します。サムネイルがない項目は標準アイコンを拡大します。縦横比を保って表示し、手動で割り当てた画像がある場合はそちらを優先します。

PNG / JPEG / BMP / ICO 等、Windows Imaging Component が読める画像に対応します。鮮明な 1024 px 表示には 1024 px 以上の画像を使ってください。サムネイルや標準アイコンの拡大では原画以上の細部は得られません。登録した画像ファイルは元の場所に保持してください。

背景の「表示」メニューから自動整列・等間隔整列を切り替えられます。適用中に変更した整列設定は解除後も維持します。整列が有効な場合、復元した位置が Windows により調整されることがあります。

### 設定と更新

既定の画像設定は `%LOCALAPPDATA%\DekaDesktopIcon\profile.ini` です。サイズと試用時間は `profile.ini.resident.ini` に保存します。画像設定の直前の内容は `.bak`、適用前の配置は `.rollback.ini`、診断情報は `.diagnostic.ini` に保存します。Explorer 再起動後の配置復旧には「保存済みの元の配置を復旧」を使用できます。

改名前のツールを使用している場合は、旧版のコントローラーで解除してから DekaDesktopIcon を適用してください。旧版のパッチが有効な場合は重ねて適用することを拒否します。新しい設定ファイルがまだない場合に限り、旧 `%LOCALAPPDATA%\ExplorerIconPatch\profile.ini` の画像割り当てをコピーします。

解除後も DLL は Explorer に読み込まれたままです。更新時は解除して別のフォルダーへビルドしてください。読み込み中の DLL を上書きできるのは Explorer 終了後です。

### コマンドライン

```powershell
.\build\bin\DekaDesktopIcon.exe --settings
.\build\bin\DekaDesktopIcon.exe --enable --size auto --lease 0 --report build\enabled.json
.\build\bin\DekaDesktopIcon.exe --disable --report build\disabled.json
.\build\bin\DekaDesktopIcon.exe --resident-state --report build\resident.json
.\build\bin\DekaDesktopIcon.exe --exit --report build\exit.json
.\build\bin\DekaDesktopIcon.exe --apply --size 1024 --lease 30 --report build\apply.json
.\build\bin\DekaDesktopIcon.exe --status --report build\status.json
.\build\bin\DekaDesktopIcon.exe --restore --report build\restore.json
```

`--enable` は常駐アプリ経由で適用し、未起動の場合は常駐を開始します。従来の `--apply` / `--restore` はコアを直接操作します。`--size auto` は自動サイズ、手動の基準サイズは 64～1024・16 px 刻みです。設定画面では自動／256／512／768／1024 を選べます。`--lease 0` は時間制限なし、`--profile <INI>` は設定ファイルの指定です。`--status` のサイズは自動調整後の実際の描画サイズです。

## 開発と検証

C++17 を使用し、`if` / `else` / `for` / `while` 等では一文でも中括弧を必須にしています。`.clang-format` と `.clang-tidy` に規則を定義しています。Visual Studio 付属または PATH 上の clang-format / clang-tidy 22 以降を使用します。

```powershell
.\scripts\Format-Source.ps1
.\scripts\Test-SourceStyle.ps1
.\validate.ps1
.\validate-imaging.ps1
.\validate-safety.ps1
.\validate-display-scaling.ps1
.\validate-resident.ps1
.\validate-relocation.ps1
.\validate-unload.ps1
.\validate-settings.ps1
.\tests\Test-SettingsPublishing.ps1
.\validate-sort.ps1
.\validate-interaction.ps1
.\validate-interaction.ps1 -DropFeedbackOnly
.\validate-shell-behavior.ps1
```

実際の Explorer を操作する回帰試験は、動作中のパッチを解除してから順に実行してください。項目の選択・移動・整列設定・メニュー表示を操作し、終了時に復元します。検証中はデスクトップの操作を控えてください。

```powershell
.\validate-explorer.ps1
.\validate-relocation.ps1 -Explorer
.\validate-explorer-sort.ps1
.\validate-interaction.ps1 -Explorer
.\validate-interaction.ps1 -Explorer -DropFeedbackOnly
.\validate-shell-behavior.ps1 -Explorer
```

各スクリプトの `-BinaryDirectory` で CMake 等の出力先を指定できます。結果は `build/validation` に出力します。操作・整列試験は 512 / 1024 px、並べ替え試験は画像リストの交換・縮小と実際の Shell コマンドを確認します。検証用の設定は通常利用の画像設定と分離しています。

`validate-display-scaling.ps1` は独立ホストの表示領域を変更し、解像度比・サイズ上下限・縦横比変更・一時的な 0 サイズ・元の解像度への復帰・描画と当たり判定・再適用・期限付き復元を検証します。実際のディスプレイ設定やリモート接続は変更しません。

構成・スレッド・復元処理については [技術メモ](docs/TECHNICAL.ja.md)、変更点は [CHANGELOG](CHANGELOG.md)、変更時の規則は [CONTRIBUTING](docs/CONTRIBUTING.md) を参照してください。

## 対応範囲

Windows 11 の Explorer が持つ `SHELLDLL_DefView` / `SysListView32` を対象とした非公式の実行時パッチです。全 Windows ビルドの互換性は保証しません。x64、同じログインセッション、最大 512 項目に対応します。管理者としての実行や追加の Visual C++ ランタイムは不要です。

1024 px のアイコンが複数ある場合は画面外にはみ出すことがあります。デスクトップ外へのドロップ等は Explorer の標準ドラッグ処理へ委譲します。混在 DPI、仮想デスクトップ、他のシェル拡張との組み合わせは未検証です。

## ライセンス

[MIT License](LICENSE)。著作権者は sh4 です。本プロジェクトの作成・リファクタリングには OpenAI Codex を使用しています。
