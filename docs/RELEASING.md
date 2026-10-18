# リリースバイナリのビルド

GitHub Actions の **Build release binaries** は Windows x64 の Release バイナリをビルドし、バージョンに対応する GitHub Release に配布用 ZIP と `SHA256SUMS.txt` を自動登録します。同じファイルを Artifact にも 14 日間保存します。

## 実行方法

1. ワークフローを既定ブランチ（現在は `main`）にマージします。
2. リポジトリの **Actions → Build release binaries → Run workflow** を開き、既定ブランチを選択します。
3. 完了後、リポジトリの **Releases** からパッケージをダウンロードします。実行の **Artifacts** にも同じ配布物があります。

実行時点のコミット SHA に固定してビルドします。バージョンは `CMakeLists.txt` の `project(... VERSION ...)` から取得します。リリース前にバージョンと `CHANGELOG.md` を更新してください。

たとえばバージョン `1.4.2` では `v1.4.2` タグと **DekaDesktopIcon 1.4.2** の Release を作成します。タグはビルドしたコミット SHA を指し、説明には `CHANGELOG.md` の該当バージョンの内容を使用します。パッケージ作成時に該当する変更履歴がない場合は停止します。

再実行では、既存のタグが同じコミットを指すことを確認してアセットと説明を更新します。別のコミットを指している場合は停止するため、コードを変更して公開する際はバージョンを上げてください。アップロード途中の失敗で残ったドラフトは、再実行でアセットを再登録してから公開します。GitHub 側で Release の不変性を有効にしている場合、公開済みアセットの置き換えはできないため、新しいバージョンで実行してください（[GitHub CLI の Release 作成仕様](https://cli.github.com/manual/gh_release_create)）。

Artifact には `DekaDesktopIcon-<version>-windows-x64.zip` と `SHA256SUMS.txt` が入ります。配布用 ZIP の内容は次の 6 ファイルです。

- `DekaDesktopIcon.exe`
- `DekaDesktopIconCore.dll`
- `LICENSE`
- `README.md`
- `CHANGELOG.md`
- `BUILD-INFO.txt`（バージョン・構成・ソースコミット）

EXE と DLL は同じフォルダーに展開します。テストプログラムや中間生成物は同梱しません。ダウンロードした ZIP の SHA-256 は PowerShell の `Get-FileHash -Algorithm SHA256 <ZIP のパス>` で照合できます。

## 実行権限と保護

ワークフローには次の制約があります。

- `workflow_dispatch` による手動実行のみです。push、タグ、PR では起動しません。
- 既定ブランチのみを受け付け、fork ではビルドジョブをスキップします。任意の ref やコマンドを入力する欄は設けていません。
- チェックアウト前に GitHub REST API で `github.actor` と `github.triggering_actor` の現在のリポジトリ権限を確認し、両方が `admin` の場合だけ続行します。Write／Maintain は許可しません。API エラーや権限不明の場合も停止します。
- 権限確認とビルドを同じジョブに置いているため、ジョブ単位の再実行でも確認を省略できません。初回の起動者の権限が後から取り消された場合も停止します。
- ワークフローの既定権限は `contents: read` とし、ビルド・公開ジョブだけ `contents: write` を許可します。Release とタグの作成・アセット更新には組み込みの `GITHUB_TOKEN` を使用し、追加の PAT や Secret は不要です。チェックアウト後の Git 設定に認証情報を残しません（[GITHUB_TOKEN の権限設定](https://docs.github.com/en/actions/tutorials/authenticate-with-github_token)）。
- 使用する公式 Action は完全なコミット SHA に固定しています。更新時は公式リポジトリのリリースと SHA を確認し、レビューして更新してください。
- GitHub ホストの `windows-2025` を使用し、実行時間を 20 分に制限します。同時ビルドは直列化し、実行中のビルドを後続の起動で中断しません。

権限確認に使う API は [Get repository permissions for a user](https://docs.github.com/en/rest/collaborators/collaborators#get-repository-permissions-for-a-user) です。再実行では初回起動者と実際の再実行者が異なるため、両方を確認します（[GitHub の再実行仕様](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/re-run-workflows-and-jobs)）。

### GitHub 側で設定する項目

YAML 内の権限確認は、非管理者によるビルドと成果物の作成を防ぎます。ただし、GitHub の標準権限では Write 権限でも手動起動・再実行を要求でき、拒否するためのジョブは起動します。また、ワークフロー自体を編集できるユーザーは確認処理を削除できます。このため、管理者は次のリポジトリ設定も適用してください。これらの設定はリポジトリ内のファイルだけでは有効になりません。

1. **Settings → Actions → Policies** に Workflow execution protections がある場合、`workflow_dispatch` の起動者をリポジトリの **Admin** ロールに制限し、Enforce にします。起動要求そのものを GitHub 側で拒否できます（[公式案内](https://github.blog/changelog/2026-06-18-control-who-and-what-triggers-github-actions-workflows/)）。
2. 既定ブランチの Ruleset／Branch protection で直接 push、force push、削除を制限し、PR とレビューを必須にします。Code Owners の承認と、変更後の承認取り消しを有効にします。`.github/CODEOWNERS` はワークフロー・CODEOWNERS 自身・ビルドスクリプト・CMake 設定の所有者を `@sh4` としています。リポジトリ移管時は更新してください。
3. **Settings → Actions → General** で既定の Workflow permissions を読み取りにし、Action による PR の作成・承認は無効にします。利用可能な Action はこのワークフローが参照する公式 Action の SHA に限定し、完全な SHA 固定を必須にします。

実行する C++ ソースも配布物の信頼性に影響するため、既定ブランチに入るコード全体をレビューしてください。SHA 固定・最小権限・CODEOWNERS の考え方は [GitHub のセキュリティガイド](https://docs.github.com/en/actions/reference/security/secure-use) を参照してください。

## ローカルでのパッケージ作成

```powershell
.\build.ps1 -Configuration Release -OutputDirectory .\build\release-bin
.\scripts\New-ReleasePackage.ps1
```

既定の出力先は `build/release` です。`-BinaryDirectory` と `-OutputDirectory` で変更できます。ローカル実行では現在の HEAD を記録するため、配布時はコミット済みのクリーンなソースを使用してください。

権限確認の回帰試験は `tests/Test-ReleaseAuthorization.ps1` で実行できます。ワークフロー内の実際の確認処理を読み込み、API をモックして、管理者の許可・非管理者や権限不明の拒否・再実行者の確認・API エラー時の停止を検証します。ネットワーク接続や認証情報は使用しません。ワークフロー自身もビルド前にこの試験を実行します。

公開処理の回帰試験は `tests/Test-ReleasePublishing.ps1` です。GitHub API と CLI をモックし、新規作成・再実行・注釈付きタグ・タグのコミット不一致・不正なチェックサム・API／アップロード／公開エラー時の停止を検証します。実際の Release は作成せず、ワークフローでもビルド前に実行します。

GitHub ホスト上での Explorer 操作を伴う回帰試験はこのワークフローには含めません。公開前の動作確認は [README](../README.md#開発と検証) に従って Windows の対話セッションで行ってください。
