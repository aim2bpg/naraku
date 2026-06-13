# Naraku Regex VM 設計ドキュメント

Naraku(奈落)の正規表現 VM(コンパイラ + 実行器)の設計ドキュメント。
mruby 上で実際にマッチングが動く最小実装の確定済み設計と、その根拠を記録する。

- **対象読者**: このプロジェクトの開発者・レビュアー。
  正規表現エンジンの内部構造を知らない読者でも読み進められるよう、
  §2 で基礎から説明する。
- **関連資料**: 機能仕様は `docs/ja/spec.md`、C 実装の設計原本は
  `ruby-prototype/lib/naraku_ruby/dfa/`(Ruby プロトタイプ)。
  挙動の疑問が出たらまずプロトタイプを参照すること。

---

## 1. このドキュメントの読み方

| 知りたいこと | 読む場所 |
|---|---|
| 正規表現エンジンの仕組みをゼロから | §2 |
| リポジトリ全体の構成と今どこまでできているか | §3, §4 |
| Onigmo と何が違うのか | §5 |
| なぜこの設計なのか(妥当性の根拠) | §6 |
| VM の実装詳細(実装再開・レビュー用) | §7 |
| mruby からどう使えるか | §8 |
| できないこと・今後の予定 | §9, §10 |

図の凡例(以降のアーキテクチャ図で共通):
🟩 **緑 = 完成(テスト済み)** / 🟨 **黄 = 着手中** / ⬜ **灰(破線)= 未着手**

---

## 2. 正規表現エンジンの基礎

### 2.1 エンジンの仕事は「コンパイル」と「実行」の 2 段階

Ruby で `"xabcbd" =~ /a(b|c)+d/` と書いたとき、裏側では次の 2 段階が動く。

```mermaid
flowchart LR
    pat["パターン文字列<br/>a(b|c)+d"] -->|コンパイル<br/>1回だけ| prog["実行可能な内部表現<br/>(状態機械のプログラム)"]
    prog -->|実行<br/>マッチのたび| result["マッチ結果<br/>位置とキャプチャ"]
    subject["対象文字列<br/>xabcbd"] --> prog
```

- **コンパイル**: パターン文字列を解析(パース)して構文木(AST)を作り、
  さらに実行しやすい「状態機械のプログラム」へ変換する。
- **実行**: そのプログラムを対象文字列の上で走らせ、
  マッチ位置とキャプチャ(`(b|c)` が何にマッチしたか等)を求める。

### 2.2 状態機械(NFA)とは

コンパイル結果の正体は **NFA(非決定性有限オートマトン)** と呼ばれる
状態機械である。難しい名前だが、要は「すごろくの盤面」だと思えばよい。
丸が「状態」、矢印が「この文字を読んだら次へ進める」という「遷移」を表す。

`a(b|c)` をコンパイルすると、こういう盤面になる:

```mermaid
stateDiagram-v2
    direction LR
    [*] --> S0
    S0 --> S1 : a を読む
    S1 --> S2 : b を読む
    S1 --> S3 : c を読む
    S2 --> [*] : マッチ成立
    S3 --> [*] : マッチ成立
```

S1 から先は「b でも c でも進める」= 道が分岐している。
この**分岐をどう処理するか**が、正規表現エンジンの方式の分かれ目になる。

### 2.3 二大方式: バックトラッキング vs Pike VM

| 観点 | バックトラッキング方式<br/>(Oniguruma / Onigmo / 多くのエンジン) | Pike VM 方式<br/>(RE2 / Rust regex / **Naraku**) |
|---|---|---|
| 分岐の進め方 | 1 本の道を奥まで試し、行き止まりなら**戻って**(backtrack)別の道を試す | 分岐したら**全部の道を同時に**、1 文字ずつ並走させる |
| 最悪の実行時間 | **指数的**(道の組み合わせが爆発する) | 常に **O(文字列長 × プログラムサイズ)** |
| ReDoS(後述) | 起こり得る | **構造的に起こらない** |
| 後方参照 `\1`・先読み | 実装しやすい | 素直には実装できない(工夫が要る) |
| 使うメモリ | 戻り先を覚えるスタック | 並走中の「スレッド」リスト(プログラムサイズに比例) |

どちらが優れているという話ではなく**トレードオフ**である。
Onigmo は機能の豊富さ(後方参照・先読きなど)を取り、
Naraku は安全な実行時間を取った(理由は §6 参照)。

### 2.4 ReDoS — バックトラッキングの弱点

`(a|a)*$` というパターンを `"aaaa...a"`(末尾に `$` を満たさない文字)に
適用すると、バックトラッキング方式では各 `a` を「左の a で読むか、右の a で
読むか」の 2 択が文字数ぶん積み重なる。

```mermaid
flowchart TD
    r["1 文字目の a"] -->|左の a で読む| n1["2 文字目の a"]
    r -->|右の a で読む| n2["2 文字目の a"]
    n1 -->|左| n3["3 文字目…"]
    n1 -->|右| n4["3 文字目…"]
    n2 -->|左| n5["3 文字目…"]
    n2 -->|右| n6["3 文字目…"]
```

n 文字で **2ⁿ 通り**の道ができ、「マッチしない」と答えるには全部の道を
試して全部失敗する必要がある。30 文字で 10 億通り——これが
**ReDoS(Regular expression Denial of Service)**: 細工した入力でサービスを
応答不能にする攻撃である。

Pike VM は同じ位置にいる重複した道を 1 つにまとめながら全道を同時に進める
ため、道の数がプログラムサイズを超えて増えない。**どんなパターンでも実行
時間が入力長に比例**し、この攻撃自体が成立しない。

---

## 3. リポジトリ全体アーキテクチャ

### 3.1 コンポーネント構成と現在地

```mermaid
flowchart TD
    subgraph support["開発支援"]
        tools["コード生成 tools/<br/>gperf + Ruby で Unicode テーブル生成<br/>→ src/.gen/ ヘッダ群"]
        proto["Ruby プロトタイプ ruby-prototype/<br/>compiler.rb 549 行 + program.rb 1,280 行<br/>テスト 932 行 = C 移植の設計原本"]
    end

    subgraph core["C コア層: src/ + include/ → build/libnaraku.a"]
        enc["エンコーディング層<br/>encoding/*.c 5 種 + encoding_ascii/unicode.c<br/>UTF-8 / Shift_JIS / ISO-8859-1 / US-ASCII / ASCII-8BIT"]
        parser["パーサー/レキサー<br/>parse.c 4,338 行(Onigmo 互換構文)"]
        ast["AST 構築・後処理<br/>node.c 385 行 / postprocess.c 832 行"]
        comp["VM コンパイラ<br/>regex_compile.c 2,234 行"]
        vm["Pike VM 実行器<br/>regex_vm.c 1,142 行"]
    end

    subgraph mrb["mruby バインディング層: mrbgems/mruby-naraku/"]
        bridgeDone["Parser / Encoding / Node ブリッジ<br/>mrb_naraku_*.c 計 1,826 行 + mrblib ラッパー"]
        bridgeMatch["マッチ API<br/>mrb_naraku_program.c + regexp.rb<br/>Naraku::Regexp / MatchData / CompileError"]
    end

    subgraph tooling["ツール・ベンチマーク"]
        repl["tools/match.rb<br/>対話型正規表現テスター(bin/mruby)"]
        bench["ruby-prototype/benchmark/bench_compare.rb<br/>Onigmo / DFA / Pike VM 3エンジン比較"]
    end

    tools -->|生成テーブル| enc
    proto -.->|移植の原本| comp
    proto -.->|移植の原本| vm
    enc --> parser
    parser --> ast
    ast --> comp
    comp --> vm
    parser --> bridgeDone
    comp --> bridgeMatch
    vm --> bridgeMatch
    bridgeMatch --> repl
    bridgeMatch --> bench

    classDef done fill:#c8e6c9,stroke:#2e7d32,color:#1b5e20
    class enc,parser,ast,tools,proto,bridgeDone,comp,vm,bridgeMatch,repl,bench done
```

読み方: 上(開発支援)→ 中(C コア)→ 下(mruby)→ 右(ツール)の 4 層。
実線矢印はビルド・実行時の依存、点線は「参照する」関係。**全コンポーネントが完成済み**。

### 3.2 なぜこの構成・この順番で作られてきたか

構成図は歴史的経緯と設計意図の両方を反映している:

1. **エンコーディング層が最初**: Naraku は「Ruby 専用」エンジンであり、
   その核心的価値は UTF-8 だけでなく Shift_JIS 等の多エンコーディングを
   ネイティブに扱えること(Onigmo の後継たる条件)。パーサーも VM も
   「1 文字読む」「この文字は `\w` か」をすべてエンコーディング API 経由で
   行うため、ここが全層の土台になる。
2. **次にパーサーと AST**: 構文の受理範囲とエラー報告(offset/length の
   span)は機能仕様そのものなので、Onigmo 互換の構文解析を先に固めた。
   テスト 2,924 行で挙動が固定されている。
3. **VM はまず Ruby プロトタイプで**: 実行エンジンは「優先度」「空ループ」
   「キャプチャの分岐コピー」など仕様の落とし穴が多い。C でいきなり書くと
   試行錯誤のコストが高いため、Ruby で先に正解を作った(§6 参照)。
4. **C 移植と mruby バインディング**: `regex_compile.c` → `regex_vm.c` を
   実装し、mruby から `Naraku::Regexp` として利用できるようになった。

### 3.3 データフロー(1 回のマッチングで起こること)

```mermaid
flowchart LR
    pat["パターン<br/>バイト列"] --> P["Parser<br/>src/parse.c"]
    P --> A["AST<br/>nk_node_t"]
    A --> PP["後処理<br/>src/postprocess.c<br/>(名前解決など)"]
    PP --> C["VM コンパイラ<br/>src/regex_compile.c"]
    C --> PR["VM プログラム<br/>nk_program_t<br/>(AST から独立)"]
    SUBJ["対象文字列<br/>バイト列"] --> V
    PR --> V["Pike VM<br/>src/regex_vm.c"]
    V --> R["マッチ結果<br/>nk_region_t<br/>(バイトオフセット)"]

    classDef done fill:#c8e6c9,stroke:#2e7d32,color:#1b5e20
    class P,A,PP,C,PR,V,R done
```

ポイント: コンパイル済みプログラム `nk_program_t` は AST にもパターン文字列
にも参照を持たない(リテラルはコンパイル時にコード点へデコード済み)。
そのためコンパイル後は AST を即座に解放でき、プログラムだけ持ち回れる。

### 3.4 ビルド・テストパイプライン

```mermaid
flowchart TD
    cg["naraku:codegen<br/>tools/gen_*.rb + gperf<br/>→ src/.gen/ と include/naraku_cprop_names.h"] --> bl["naraku:build_lib<br/>make build/libnaraku.a<br/>(src/*.c を wildcard で自動検出)"]
    bl --> bm["naraku:build_mruby<br/>mruby 本体 + mruby-naraku gem をリンク<br/>→ bin/mruby"]
    bm --> tm["naraku:test_mruby<br/>bin/mruby test/test_run.rb<br/>(Mtest フレームワーク)"]
    bm2["naraku:build_mruby_asan<br/>AddressSanitizer ビルド(clang)"] --> tm
    pt["ruby_prototype:test<br/>C ビルド不要・独立で実行可"]
    pb["ruby_prototype:benchmark<br/>synthetic / corpus / compare スイート"]
```

新しい `.c` ファイルは Makefile の wildcard が自動で拾うため、
`regex_compile.c` / `regex_vm.c` の追加にビルド設定の変更は不要。

---

## 4. 実装ステータス

### 4.1 このブランチ(`kansai-rubykaigi09`)で追加・変更したもの

このブランチが作られた時点では、Naraku は**パーサー専用エンジン**だった。
`Naraku::Parser.new(enc, pattern).parse` で AST を得ることはできたが、
その先でマッチングを行う手段がなかった。

このブランチで追加した変更の一覧:

| 変更 | ファイル | 内容 |
|---|---|---|
| ✨ 公開 API ヘッダ | `include/naraku_regex.h` | `nk_program_compile` / `nk_program_search` ほか公開型定義 |
| ✨ VM コンパイラ | `src/regex_compile.c` | AST → `nk_program_t` バイトコードコンパイラ(2,234 行) |
| ✨ Pike VM 実行器 | `src/regex_vm.c` | Pike VM マッチング実行器(1,142 行) |
| ✨ コンパイラ用エラーコード | `include/naraku_error.h`, `src/error.c` | `-600` 番台 `NK_ERR_UNSUPPORTED_*` ほか 10 種 |
| ✨ mruby マッチ API | `mrbgems/mruby-naraku/src/mrb_naraku_program.c` | `Naraku::Program` C ブリッジ |
| ✨ mruby Ruby ラッパー | `mrbgems/mruby-naraku/mrblib/naraku/regexp.rb` | `Naraku::Regexp` / `MatchData` / `CompileError` |
| ✨ Mtest テスト | `test/regexp_test.rb` | 46 テストケース(マッチング全体を網羅) |
| ✨ 対話型テスター | `tools/match.rb` | `bin/mruby` で動く Rubular ライク CLI |
| ✨ 3 エンジン比較ベンチマーク | `ruby-prototype/benchmark/bench_compare.rb` + `tools/bench_pike_vm.rb` | Onigmo / DFA / Pike VM の速度比較 |
| 🐛 バグ修正 | `src/cprop.c` | `code_in_code_range` の `size_t` アンダーフロー修正(`\b` 誤判定の根本原因) |

**ブランチ前後の能力差**:

| 操作 | ブランチ前 | ブランチ後 |
|---|---|---|
| `Naraku::Parser.new(enc, pat).parse` | ✅ | ✅ |
| `Naraku::Regexp.new(pat).match(str)` | ❌ | ✅ |
| `Naraku::MatchData` でキャプチャを取得 | ❌ | ✅ |
| 未対応機能が `CompileError` になる | ❌(サイレント) | ✅ |
| `bin/mruby tools/match.rb` で即試せる | ❌ | ✅ |
| Onigmo / DFA / Pike VM の速度比較 | ❌ | ✅ |
| `\b` が正しく動く | ❌(バグあり) | ✅ |

### 4.2 全コンポーネント一覧

| コンポーネント | 場所 | 規模 | テスト | ブランチ前 |
|---|---|---|---|---|
| エンコーディング層(5 種) | `src/encoding/`, `src/encoding_*.c` | 約 1,000 行 + 生成テーブル | ✅ 約 900 行 | 🟩 既存 |
| パーサー/レキサー | `src/parse.c` | 4,338 行 | ✅ 1,823 行 | 🟩 既存 |
| AST・後処理 | `src/node.c`, `src/postprocess.c` | 1,217 行 | ✅ あり | 🟩 既存 |
| Unicode コード生成 | `tools/gen_*.rb` | 約 400 行 | (生成物をテストで担保) | 🟩 既存 |
| Ruby プロトタイプ(VM の原本) | `ruby-prototype/lib/naraku_ruby/` | 2,513 行 | ✅ 932 行 | 🟩 既存 |
| mruby バインディング(Parser/Encoding/Node) | `mrbgems/mruby-naraku/` | C 1,826 行 + Ruby 284 行 | ✅(test/ 経由) | 🟩 既存 |
| **VM コンパイラ** | `src/regex_compile.c` | 2,234 行 | ✅ `test/regexp_test.rb` | ✨ **追加** |
| **Pike VM 実行器** | `src/regex_vm.c` | 1,142 行 | ✅ `test/regexp_test.rb` | ✨ **追加** |
| コンパイラ用エラーコード | `naraku_error.h`, `error.c` | -600〜-610 追加 | ✅ `test/regexp_test.rb` | ✨ **追加** |
| 公開 API ヘッダ | `include/naraku_regex.h` | 213 行 | — | ✨ **追加** |
| mruby マッチ API | `mrb_naraku_program.c`, `regexp.rb` | C 143 行 + Ruby 129 行 | ✅ 46 テスト | ✨ **追加** |
| 対話型テスター | `tools/match.rb` | 75 行 | — | ✨ **追加** |
| 3 エンジン比較ベンチマーク | `benchmark/bench_compare.rb` + `tools/bench_pike_vm.rb` | 計 255 行 | — | ✨ **追加** |
| `\b` バグ修正 | `src/cprop.c` | `code_in_code_range` 修正 | ✅(既存テストが通る) | 🐛 **修正** |

---

## 5. Onigmo との比較

### 5.1 位置づけ

Naraku(奈落)は Oniguruma(鬼車)→ Onigmo(鬼雲)の系譜に連なる
**Ruby 専用の後継エンジン**を目指すプロジェクト。Onigmo は CRuby に長年
組み込まれてきた実績あるエンジンだが、C 実装としての古さ(保守性)と
バックトラッキング方式に由来する ReDoS が構造的課題として残る。

### 5.2 機能比較

| 機能 | Onigmo | Naraku(現在) | Naraku 将来版 |
|---|---|---|---|
| リテラル・連接・選択 `\|` | ✅ | ✅ | ✅ |
| `.`(任意の 1 文字) | ✅ | ✅ | ✅ |
| 量指定子 `* + ? {m,n}`(greedy/lazy) | ✅ | ✅ | ✅ |
| 文字クラス(範囲・否定・`&&`・POSIX・`\p{...}`) | ✅ | ✅ | ✅ |
| キャプチャ・非捕捉グループ | ✅ | ✅(番号のみ) | ✅(名前付き含む) |
| アンカー `^ $ \A \z \Z \G \b \B` | ✅ | ✅ | ✅ |
| `\K`(マッチ開始のリセット) | ✅ | ✅ | ✅ |
| possessive 量指定子 `a*+` | ✅ | ❌ 明示エラー | ⭕ 予定 |
| `/i`(ASCII-only / Simple / Full Unicode fold) | ✅ | ✅ | ✅ |
| 後方参照 `\1` `\k<name>` | ✅ | ❌ 明示エラー | ⭕ 予定 |
| 先読み・後読み `(?=) (?!) (?<=) (?<!)` | ✅ | ❌ 明示エラー | ⭕ 予定 |
| アトミックグループ `(?>)`・不在 `(?~)`・条件分岐 | ✅ | ❌ 明示エラー | ⭕ 検討 |
| 部分式呼び出し `\g<...>` | ✅ | ❌ 明示エラー | ⭕ 検討 |
| `\R` `\X` その他 | ✅ | ❌ 明示エラー | ⭕ 予定 |
| エンコーディング | 約 30 種 | 5 種 | 順次拡張 |
| ReDoS 耐性 | ❌(タイムアウトで緩和) | ✅ 構造的に安全 | ✅ |

重要: ❌ の機能は**パーサーは受理する**(構文解析は Onigmo 互換で完成済み)
が、**コンパイル時に `NK_ERR_UNSUPPORTED_*` で明示的に失敗**する。
黙って違う結果を返すことはない(§6 の設計判断)。

### 5.3 実行方式の理論比較と実測

**理論比較:**

| 観点 | Onigmo(バックトラッキング) | Naraku(Pike VM) |
|---|---|---|
| 時間計算量 | 平均は高速、**最悪 O(2ⁿ)** | **常に O(n × m)**(n=文字列長, m=プログラムサイズ) |
| 空間計算量 | バックトラックスタック(入力依存で伸びる) | スレッドリスト O(m) + キャプチャ |
| ReDoS | パターン次第で発生 | 発生しない |
| 単純パターンの定数倍 | 軽い(歴代の最適化の蓄積) | スレッド管理のオーバーヘッドあり |
| 機能の実装自由度 | 高い(後方参照・先読みが自然に書ける) | 制限あり(将来はハイブリッド等の工夫が必要) |

**実測ベンチマーク** (`ruby-prototype/benchmark/bench_compare.rb`):

> 測定環境: CRuby 4.0.5 / mruby 4.0.0、Dev Container (x86_64)。
> ips = 全入力バッチを 1 周する回数/秒(高いほど速い)。
> Naraku Pike VM は最適化サイクル A〜M 適用後の値。

**JIT なし:**

| パターン | Onigmo | NarakuRuby LazyDFA | Naraku Pike VM | Naraku/Onigmo |
|---|---:|---:|---:|---:|
| `Watson` (literal) | 3,735 | 280 | 2,540 | 0.68x |
| `foo\|bar\|baz` (alternation) | 3,727 | 541 | 2,300 | 0.62x |
| `a+b` (repetition) | 1,721 | 32 | 1,030 | 0.60x |
| `(a\|a)+b` (ambiguous) | 719 | 29 | 968 | **1.35x** ↑ |
| `[a-zA-Z0-9]+` (char_class) | 3,901 | 1,003 | 3,500 | **0.90x** |
| `\d{4}-\d{2}-\d{2}` (bounded) | 3,549 | 249 | 2,650 | 0.75x |
| `(?:a?){30}a{30}` (pathological) | 808 | 67 | 10 | 0.01x |
| `ジョバンニ\|カムパネルラ` (unicode) | 3,657 | 133 | 2,650 | 0.72x |

**YJIT 有効 (`ruby --yjit`):**

| パターン | Onigmo+YJIT | NarakuRuby LazyDFA+YJIT | LazyDFA YJIT 倍率 | Naraku VM/Onigmo+YJIT |
|---|---:|---:|---:|---:|
| `Watson` (literal) | 4,766 | 566 | **2.0x** | 0.47x |
| `foo\|bar\|baz` (alternation) | 4,687 | 1,123 | **2.1x** | 0.46x |
| `a+b` (repetition) | 1,806 | 63 | **2.0x** | 0.57x |
| `(a\|a)+b` (ambiguous) | 686 | 63 | **2.2x** | **1.40x** ↑ |
| `[a-zA-Z0-9]+` (char_class) | 4,823 | 2,312 | **2.3x** | 0.66x |
| `\d{4}-\d{2}-\d{2}` (bounded) | 4,136 | 523 | **2.1x** | 0.50x |
| `(?:a?){30}a{30}` (pathological) | 846 | 109 | 1.6x | 0.01x |
| `ジョバンニ\|カムパネルラ` (unicode) | 4,574 | 247 | **1.9x** | 0.42x |

**読み方:**
- `ambiguous: (a|a)+b` — Pike VM が **JIT なしで Onigmo+YJIT を 1.40x 超え**。Lazy DFA + キャッシュ(G-3)でバックトラッキングの指数的な遷移数を O(1) に圧縮できるため。
- LazyDFA(Ruby) は YJIT で **約 2×** に加速(P6a の設計意図)。C 実装の Pike VM は JIT の恩恵を受けないが、YJIT 無効の Onigmo も超えられるパターンがある。
- `pathological` — 64 状態超のため bitset 最適化は未適用。それでも **O(n) で完走**。ReDoS 耐性は確認済み。

---

## 6. 設計判断とその根拠

「なぜ今の設計なのか」を判断ごとに記録する(Decision Record)。

| # | 判断 | 採った選択 | 検討した代替案 |
|---|---|---|---|
| D1 | 実行方式 | Pike VM | バックトラッキング VM |
| D2 | 開発フロー | Ruby プロトタイプ → C 移植 | C で直接実装 |
| D3 | 配布先 | mruby 先行 | CRuby gem 同時 |
| D4 | 未対応機能の扱い | コンパイル時に明示エラー | 受理して部分的に動かす |
| D5 | 文字クラス表現 | inversion list + ASCII ビットマップ | 全域ビットマップ |
| D6 | キャプチャ | 参照カウント COW | スレッドごとに配列コピー |
| D7 | ε 閉包の実装 | 明示スタックで反復 | 再帰 |
| D8 | 複雑度上限 | 状態数 2¹⁸ / ε id 64 | 無制限 |

**D1: Pike VM を採る。** 最大の理由は移植リスクの最小化:設計原本である
Ruby プロトタイプ(テスト 932 行で挙動固定)が Pike VM 方式であり、同方式で
移植すればテスト資産と優先度セマンティクスをそのまま検証に使える。加えて
ReDoS 耐性が「機能」として無料で付いてくる(§2.4)。トレードオフとして
後方参照・先読みが素直に書けないが、これらは元々スコープ外であり、
将来版で部分的バックトラッキングとのハイブリッド等を検討する。

**D2: プロトタイプ先行。** 正規表現エンジンの難所はアルゴリズムの正しさ
(greedy/lazy の優先順位、空マッチループの停止、分岐時のキャプチャ複製)で
あり、C のメモリ管理と同時に格闘すると手戻りが大きい。Ruby で仕様の正解を
固めたので、C 側は「答え合わせのできる移植」に専念できる。実際、
`compiler.rb`(549 行)と `program.rb`(1,280 行)が
`regex_compile.c` / `regex_vm.c` の 1:1 の下敷きになっている。

**D3: mruby 先行。** mrbgems バインディング基盤(Parser/Encoding/Node、
C 1,826 行)が既に完成しており、マッチ API を足すだけで「動くもの」に
到達できる。CRuby gem は extconf.rb・gemspec・CI の整備一式が必要で、
最短リリースの障害になるため後続に送った。

**D4: 未対応は明示エラー。** パーサーは Onigmo 互換でフル構文を受理する
ため、コンパイラが黙って未対応構文を無視すると「エラーにならないのに
結果が違う」という最悪の振る舞いになる。`NK_ERR_UNSUPPORTED_*`(-600 番台)
で機能別に失敗し、エラー位置(offset/length)も返す。

**D5: inversion list。** Unicode は約 111 万コード点あり、文字クラスごとの
ビットマップ(136KB)は非現実的。ソート済み区間列 `[lo, hi]` なら
`\p{Lu}` でも数百区間で済み、和・積・否定が単純な区間演算になる。
頻出の ASCII 範囲だけ `uint64_t[2]` のビットマップを併設して O(1) 判定する。

**D6: COW キャプチャ。** Pike VM は SPLIT のたびにスレッドが分岐し、素朴に
キャプチャ配列をコピーすると O(スレッド数 × キャプチャ数) のコピーが
毎文字発生する。参照カウントを付けて書き込み時のみ複製することで、
分岐は参照カウントのインクリメント 1 回になる。

**D7: 反復 ε 閉包。** ε 遷移(文字を消費しない状態移動)の連鎖を再帰で
たどると、深くネストしたパターンで C スタックが溢れる。明示スタック +
作業アイテム方式なら深さはヒープ上限までで、優先度順序も
「split_next → next の順に積む(LIFO で next が先に処理される)」ことで
保存できる。

**D8: 上限を設ける。** `a{1000000}` は min 回展開で状態数が爆発するため
状態数上限 2¹⁸(`NK_MAX_VM_STATES`)で `NK_ERR_PATTERN_TOO_COMPLEX` を
返す。空マッチ可能ループの管理 id はスレッドが持つ `uint64_t` 1 個の
ビット集合で足りる 64 個まで(`NK_MAX_VM_EPSILON_CHECK_IDS`)。
実用パターンで触れる上限ではなく、悪意ある入力への防御線である。

---

## 7. VM 詳細設計

### 7.1 命令セット(`nk_vm_op_t`)

プログラムは「状態」(`nk_vm_state_t`)の配列で、各状態が 1 命令を持つ。

| 命令 | 意味 | 消費 | 主フィールド |
|---|---|---|---|
| `CODE` | コード点 1 個と一致 | 1 文字 | `code`, `check_id` |
| `CHAR_CLASS` | 文字クラスと一致 | 1 文字 | `char_class_index`, `check_id` |
| `DOT` | 任意の 1 文字(`.`) | 1 文字 | `allows_newline`, `check_id` |
| `ASSERTION` | ゼロ幅アサーション | ε | `assertion_type` |
| `CAP_BEGIN` / `CAP_END` | キャプチャ位置記録 | ε | `cap_num` |
| `KEEP` | `\K`(報告するマッチ開始位置をリセット) | ε | — |
| `JUMP` | 無条件 ε 遷移 | ε | `next` |
| `SPLIT` | 優先分岐(`next` 優先、`split_next` 劣後) | ε | `next`, `split_next` |
| `CHECK_VISITED` | ε 経路合流点の重複排除 | ε | `check_id` |
| `MARK_EPSILON` | 空マッチ可能ループ本体への突入を記録 | ε | `check_id`(ε 用 id) |
| `CHECK_EPSILON` | 本体が空マッチなら `split_next`(脱出)、消費していれば `next`(継続) | ε | `check_id`, `next`, `split_next` |
| `MATCH` | 受理 | — | `check_id` |

- 「消費 = ε」は文字を読まずに移動できる命令(ε 遷移)。
- `check_id`: 文字消費命令 + `MATCH` + `CHECK_VISITED` に一意採番される
  重複排除キー。ε 閉包内で同じ id を二度通らないために使う。
- `MARK_EPSILON` / `CHECK_EPSILON` の id は別系列で、上限 64
  (スレッドの `epsilon_bits` を `uint64_t` 1 個で持つため。D8)。

### 7.2 コンパイラ(`src/regex_compile.c`)

#### フラグメントと hole-patching

`compile_node()` は AST ノードを「入口状態 + 未接続の出口(hole)リスト」の
フラグメントに変換する。hole は `(state_index, next か split_next か)` の組で、
後続フラグメントの入口が決まった時点で `patch_all()` が埋める。

- **`patch_all()` は hole が 2 個以上のとき `CHECK_VISITED` を 1 個発行**して
  合流させる(ε 閉包の指数的爆発防止。プロトタイプと同じ)。

#### コンパイル例: `a(b|c)+d`

```mermaid
flowchart LR
    s0["CAP_BEGIN 0"] --> s1["CODE a"]
    s1 --> s2["CAP_BEGIN 1"]
    subgraph first["1 回目(+ の必須分)"]
        s2 --> s3{"SPLIT"}
        s3 -->|優先| s4["CODE b"]
        s3 -->|劣後| s5["CODE c"]
        s4 --> s6["CAP_END 1"]
        s5 --> s6
    end
    s6 --> s7{"SPLIT<br/>ループ"}
    subgraph loop["2 回目以降(ループ本体)"]
        s8["CAP_BEGIN 1"] --> s9{"SPLIT"}
        s9 -->|優先| s10["CODE b"]
        s9 -->|劣後| s11["CODE c"]
        s10 --> s12["CAP_END 1"]
        s11 --> s12
    end
    s7 -->|優先: もう 1 回| s8
    s12 --> s7
    s7 -->|劣後: 抜ける| s13["CODE d"]
    s13 --> s14["CAP_END 0"] --> s15["MATCH"]
```

(重複排除用の `CHECK_VISITED` は図では省略。)
`a+` は「1 回分を展開 + ループ」に分解される。greedy なので SPLIT の
**優先側がループ継続**(lazy なら逆)。プログラム全体は必ず
`CAP_BEGIN(0) → 本体 → CAP_END(0) → MATCH` の形になる
(空パターンなら本体なし)。

#### 量指定子の展開規則

- `{min,}` / `{min,max}` は **min 回展開**(child を min 回コンパイルして
  連結)+ 残りを `at_most_n`(max−min 個の SPLIT チェーン)または
  `zero_or_more`(SPLIT 1 個 + ループバック)で構成
- greedy はループ本体を `split.next`(優先)に、lazy(reluctant)は
  `split.split_next` に置く
- possessive は `NK_ERR_UNSUPPORTED_POSSESSIVE_QUANTIFIER`
- **空マッチ可能な本体を持つ無限ループ(`zero_or_more`)のみ**
  `MARK_EPSILON → 本体 → CHECK_EPSILON` で包む(ε 無限ループ防止)。
  有限の `at_most_n` は自然に停止するので包まない(ε id の節約。
  プロトタイプとの意図的な差分)
- 空マッチ可能判定 `node_can_match_empty()`: literal は buf 空、
  消費系は false、assertion/keep は true、quantifier は `min==0 || child`、
  concat は全子、alt はいずれかの子、group/capture は child(NULL は true)

#### 文字クラス(`cc_set_t`)

ソート済み・非重複・両端含む `[lo, hi]` ペア列(inversion list)で構築する。

- `cc_add_range`: 隣接(`lo-1` / `hi+1`)も含めてマージ。プロトタイプ
  `char_class.rb` の `add` と同じアルゴリズム(C 版は線形探索)
- `cc_intersect`: two-pointer、`cc_negate`: 全体集合 `[0, 0x10FFFF]` との差
- クラスノードの構造は「union(項目の和)を `&&` で intersect、最後に
  `[^...]` なら negate」。ネストクラスは再帰
- 文字プロパティ(`\w` `\d` `\s` `\h`、POSIX、`\p{...}`)は
  `nk_enc_get_cprop_code_range()` で取得:
  - `NK_ENC_NO_DELEGATION`: intervals(**len はペア数**)をコピー
  - `NK_ENC_7BIT_DELEGATE` / `8BIT`: `0..0x7F` / `0..0xFF` を
    `nk_enc_code_is_cprop()` で列挙
  - ascii_only(`a` フラグ等)なら `[0,0x7F]` と intersect、
    負(`\D` 等)なら negate
- マッピング: `\w`→`NK_CPROP_WORD`、`\d`→`DIGIT`、`\s`→`SPACE`、
  `\h`→`XDIGIT`。POSIX は同名 cprop(`[[:word:]]`→`WORD`、
  `[[:punct:]]`→`PUNCT`)
- 実行時用に `nk_vm_char_class_t` へ変換: ranges コピー + ASCII 高速パスの
  `uint64_t ascii_bits[2]` ビットマップ(D5)

#### リテラル

`nk_pbuf_t` のバイト列をコンパイル時に `nk_enc_scan_mbc_width` /
`nk_enc_decode_mbc` でデコードし、コード点ごとの `CODE` 命令列にする。
コンパイル後のプログラムは AST・パターン文字列から独立する(§3.3)。

#### ケースフォールディング(`/i` フラグ)

`/i` フラグ付きコンパイルは、fold の種類ごとに異なる戦略を採る。

| フラグ | 種類 | 戦略 |
|--------|------|------|
| `NK_FOLD_ASCII_ONLY` | A-Z ↔ a-z のみ | **実行時 fold**: `CODE` 状態に `is_ignore_case` を付与し、VM が `tolower()` 相当で両辺を比較 |
| `NK_FOLD_DEFAULT` (Simple) | 1対1 Unicode fold(ä↔Ä 等) | **実行時 fold**: 同上。`nk_enc_get_case_fold()` で両辺を fold して比較 |
| `NK_FOLD_FULL` | 1対多 Unicode fold(ß→ss 等) | **コンパイル時展開**: `compile_full_fold_seq()` で交替形 NFA に展開 |
| `NK_FOLD_TURKISH_AZERI` | トルコ語/アゼルバイジャン語 fold | **コンパイル時展開**: `compile_full_fold_seq()` で展開(I↔ı 等) |

**ASCII-only / Simple fold のリテラル**: `CODE` 状態に `is_ignore_case = true` と `fold_flags` を付与する。VM の `state_matches_code()` が実行時に fold して比較する。

**文字クラスのケースフォールディング**: fold 種別によらずコンパイル時に `cc_set_t` を展開する。

- `NK_FOLD_ASCII_ONLY`: `cc_ascii_fold_expand()` — A-Z ↔ a-z の対を `cc_set_t` に追加
- それ以外: `cc_simple_fold_expand()` — `nk_enc_iterate_case_fold()` で全ペアをスキャン

`cc_simple_fold_expand()` のコールバックは `fold_len != 1` をスキップするため、ß のような 1対多 fold は文字クラスで自動無視される(文字クラスは常に 1 文字にマッチするため、意味的にも正しい)。

**Full fold のリテラル展開アルゴリズム(`compile_full_fold_seq`)**:

```
1. パターン literal を get_case_fold() で正規化 → folded_codes[]
   例: "ß" → [s, s]   "iß" → [i, s, s]

2. folded_codes[] を再帰的に処理:
   各位置 p で…
     a. identity = folded_codes[p] そのもの (len=1)
     b. expand_case_unfold(folded_codes[p..], FULL) で追加の code point を取得
        例: [s, s] → {S(len=1), ß(len=2)} を返す
     c. identity + expand 結果 = 交替形リスト
        例: [s, s] → {s(len=1), S(len=1), ß(len=2)}

     ─── 指数爆発防止 ───
     d. 同じ len を持つ交替形は継続を 1 回だけコンパイルして共有
        (len=1 の s/S は同じ cont[p+1] を参照 → 線形状態数)
     ─────────────────

     e. SPLIT チェーン + CODE ステートとして emit
        CODE.next = 共有された継続の initial state

3. 生成された NFA は通常の CODE ステート(is_ignore_case=false)なので VM 無改修
```

```mermaid
flowchart LR
    subgraph "folded=[s,s]"
        SP0{SPLIT} -->|next| Cs["CODE(s)"]
        SP0 -->|split_next| SP1{SPLIT}
        SP1 -->|next| CS["CODE(S)"]
        SP1 -->|split_next| CB["CODE(ß)"]
    end
    subgraph "cont[s]"
        SP2{SPLIT} -->|next| Cs2["CODE(s)"]
        SP2 -->|split_next| CS2["CODE(S)"]
    end
    Cs --> SP2
    CS --> SP2
    CB --> done["ε (hole)"]
    Cs2 --> hole["ε (hole)"]
    CS2 --> hole
```

(共有された `cont[s]` を s と S の両 CODE が参照することで状態数が O(folded\_len) になる。)

#### エラー方針

- 未対応機能はノード種別ごとに `NK_ERR_UNSUPPORTED_*`(-600 番台)を返す
  (§9 の一覧)
- エラー span は**最内ノード**の `span_offset` / `span_length` を採用
  (ディスパッチャでラップし、最初にエラーを返した深さで記録。
  `has_error_span` フラグで外側の上書きを防ぐ)
- プロジェクト規約どおり offset と length の両方を必ず返す

### 7.3 Pike VM 実行器(`src/regex_vm.c`)

#### 全体の動き

```mermaid
sequenceDiagram
    participant Main as メインループ
    participant Closure as ε 閉包(明示スタック)
    participant Threads as スレッドリスト(優先度順)
    participant Caps as キャプチャ(参照カウント COW)

    Main->>Closure: 初期状態を展開(開始位置)
    Closure->>Caps: SPLIT で fork(参照カウント +1)
    Closure-->>Threads: 優先度順のスレッド列が完成
    loop 文字位置ごと(1 文字ずつ前進)
        Main->>Threads: 現在の文字と各スレッドの命令を照合
        Threads-->>Main: 一致したスレッドだけ次状態へ
        Main->>Closure: 次状態から ε 閉包を展開
        Closure->>Caps: CAP_BEGIN/END で位置書き込み(共有中ならコピー)
        Note over Main,Threads: 非アンカー検索: マッチ未確定の間、<br/>各位置で初期状態を最低優先度で再注入
        Main->>Main: MATCH 到達 → 結果を記録し、低優先スレッドを破棄
    end
    Main-->>Main: threads[0] が MATCH なら探索終了(leftmost-first)
```

#### スレッドとキャプチャ(COW)

- スレッド = `{ state_index, keep_pos, epsilon_bits, caps* }`
- caps は参照カウント式 COW: `caps_t { uint32_t refcount; size_t data[]; }`
  (C99 flexible array member)。SPLIT で fork(refcount++)、書き込み時に
  refcount > 1 ならコピー(D6)
- `data` レイアウトは `[begin_0, end_0, begin_1, end_1, ...]`、
  **バイトオフセット**(プロトタイプは文字位置だが C API は Onigmo 流)。
  未設定は `NK_REGION_POS_NONE`(SIZE_MAX)
- マッチ確定時の materialize で `data[0] = keep_pos`(`\K` 対応)

#### ε 閉包(反復・明示スタック)

再帰せず明示スタックで実装する(D7)。

- 作業アイテム = `{ is_mark, state_index, keep_pos, epsilon_bits, caps* }`
- SPLIT は **split_next → next の順で push**(LIFO で next 側が先に処理され
  優先度が保存される)。caps は fork し、原本を split_next 側へ
- CHECK_VISITED: visited 済みなら枝刈り。未訪問なら「mark するアイテム」を
  先に push してから next を push(= post-order でマーク。プロトタイプの
  「再帰後にマーク」と同じ)
- visited 判定は `num_check_ids` 長の配列 + visit トークン(世代カウンタ)を
  インクリメントして使い回す
- MARK_EPSILON: `epsilon_bits |= 1 << id`。CHECK_EPSILON: bit が立っていれば
  split_next(脱出)のみ、立っていなければ next(継続)のみ。bit は文字を
  消費した時点でクリア(次位置の閉包は bits=0 から)

#### メインループ(ストリーム処理)

- `prev_code` / `curr_code` / `next_code` の 3 文字窓を維持
  (`NO_CHAR = UINT32_MAX` を番兵に)。アサーション評価に使う
- 各文字境界で: 現スレッド列のうち現在文字に一致する消費命令のスレッドを
  次位置へ進め、ε 閉包を取って次スレッド列を構成 → スワップ
- **非アンカー検索**: マッチ未確定の間、各位置で初期状態を最低優先度で再注入
- スレッドが MATCH に到達したら region を確定し、それより低優先のスレッドを
  破棄。`threads[0]` が MATCH なら探索終了
- 不正バイト列は `NK_ERR_INVALID_BYTE_SEQUENCE`

#### アサーション評価

| 種別 | 条件 |
|---|---|
| `^` | `pos == 0 \|\| prev == '\n'` |
| `$` | 末尾 `\|\| curr == '\n'` |
| `\A` | `pos == 0` |
| `\z` | 末尾 |
| `\Z` | 末尾 `\|\| (curr == '\n' && その直後が末尾)` |
| `\G` | `pos == start_pos` |
| `\b` / `\B` | `is_word(prev) != is_word(curr)` / 同値。`is_word` は `nk_enc_code_is_cprop(enc, code, NK_CPROP_WORD)`(ASCII 版は `code < 0x80 &&`) |

---

## 8. mruby バインディング

### 8.1 呼び出しの流れ

```mermaid
sequenceDiagram
    actor User as ユーザーコード
    participant Re as Naraku::Regexp(mrblib)
    participant Bridge as C ブリッジ(mrb_naraku_*.c)
    participant Lib as libnaraku.a

    User->>Re: Naraku::Regexp.new("a(b|c)+d")
    Re->>Bridge: Parser._new → _parse
    Bridge->>Lib: nk_parser_parse / nk_parser_postprocess
    Lib-->>Bridge: AST(nk_node_t)
    Re->>Bridge: Program._compile(parser, node)
    Bridge->>Lib: nk_program_compile
    Lib-->>Bridge: nk_program_t(失敗時 NK_ERR_UNSUPPORTED_* + span)
    Bridge-->>Re: program(失敗時 Naraku::CompileError)
    User->>Re: re.match("xabcbd")
    Re->>Bridge: program._search(str, 0)
    Bridge->>Lib: nk_program_search
    Lib-->>Bridge: nk_region_t(バイトオフセット)
    Bridge-->>Re: Integer 配列 or nil
    Re-->>User: Naraku::MatchData or nil
```

### 8.2 公開 API

```ruby
# コンパイル
re = Naraku::Regexp.new("a(b|c)+d")           # Naraku::CompileError を raise することがある

# マッチ
md = re.match("xabcbd")                        # => Naraku::MatchData or nil
md[0]          # => "abcbd"  (マッチ全体)
md[1]          # => "b"      (キャプチャグループ 1)
md.byte_begin(0)  # => 1
md.byte_end(0)    # => 6
md.pre_match   # => "x"
md.post_match  # => ""
md.captures    # => ["b"]

re.match?("xabcbd")  # => true
re =~ "xabcbd"       # => 1  (マッチ開始バイトオフセット)

# 対話型テスター
# bin/mruby tools/match.rb PATTERN SUBJECT
# bin/mruby tools/match.rb  (REPL モード)
```

### 8.3 実装メモ

- `Naraku::Program._compile(parser, node)` → C 側で
  `nk_program_compile(parser->enc, node, parser->num_capture_groups, ...)`。
  失敗時は `Naraku::CompileError`(offset/length 付き、既存 `ParseError` と
  同形式のメッセージ整形)
- `program#_search(string, byte_start)` → マッチ時はバイトオフセットの
  Integer 配列、非マッチは nil
- `mrb_data_type` + free hook で `nk_program_t` を解放(既存
  `mrb_naraku_parser.c` のパターン踏襲)
- キャプチャ配列は flat Integer 配列 `[begin_0, end_0, begin_1, end_1, ...]`
  で渡す。`NK_REGION_POS_NONE`(SIZE_MAX)は nil に変換

---

## 9. 未対応機能(コンパイル時に明示エラー)

| 機能 | エラー |
|---|---|
| 後方参照 `\1` `\k<name>` | `NK_ERR_UNSUPPORTED_BACK_REF` |
| 部分式呼び出し `\g<...>` | `NK_ERR_UNSUPPORTED_SUBEXP_CALL` |
| 先読み・後読み `(?=) (?!) (?<=) (?<!)` | `NK_ERR_UNSUPPORTED_LOOKAROUND` |
| アトミックグループ `(?>)` | `NK_ERR_UNSUPPORTED_ATOMIC_GROUP` |
| 不在グループ `(?~)` | `NK_ERR_UNSUPPORTED_ABSENCE_GROUP` |
| 条件分岐 `(?(...)...)` | `NK_ERR_UNSUPPORTED_CONDITIONAL` |
| possessive 量指定子 `a*+` | `NK_ERR_UNSUPPORTED_POSSESSIVE_QUANTIFIER` |
| `\R` `\X` その他 | `NK_ERR_UNSUPPORTED_FEATURE` |
| プログラム上限超過(状態数 2¹⁸、ε id 64) | `NK_ERR_PATTERN_TOO_COMPLEX` |

---

## 10. パフォーマンス改善ロードマップ

### 10.1 施策の全体像

Pike VM は正確さと ReDoS 耐性を優先した素直な実装をベースに、
3 つのアプローチで Onigmo に近づける。

```mermaid
flowchart LR
    Input["入力バイト列"]

    subgraph pre["① プリフィルタ\n(NFA を動かす前に絞る)"]
        P2["P2: memmem プリスキャン"]
        P7a["P7a: alt multi-memmem"]
        P7b["P7b: 必須バイト memchr"]
    end

    subgraph bypass["② パターン別 NFA バイパス\n(NFA を完全スキップ)"]
        P9a["P9a/b/c: リテラル・交替形\n→ memmem 結果で即 return"]
        P10["P10: char-class ループ\n→ ascii_lookup scan で即 return"]
        PL["L: first-byte table jump\n→ 候補外バイトをまとめてスキップ"]
    end

    subgraph nfa["③ NFA 高速化\n(NFA を動かすが内部を最適化)"]
        P1["P1: no-caps モード"]
        P5["P5: Thompson bitset"]
        P6b["P6b: Lazy DFA (C)"]
        P3P4["P3/P4: ASCII ランスキャン"]
        P8["P8: ascii_lookup テーブル"]
    end

    Input --> pre --> bypass
    pre --> nfa
    bypass -->|"即 return"| Result["NK_SUCCESS / NK_NO_MATCH"]
    nfa --> Result
```

P6a(Ruby 実装 Lazy DFA)のみ YJIT/ZJIT の恩恵を受ける。P1〜P5・P6b〜P10 は C 実装のため JIT と無関係。

### 10.2 各サイクルの決定的ポイント

各サイクルは「実装 → ベンチマーク確認 → コミット」を 1 単位とした。
**ここが決定的**列はそのサイクルで最も効いたメカニズムの核心を 1 文で示す。

| Cycle | 施策 | ここが決定的 | 主なターゲット | 代表改善幅 |
|-------|------|------------|-------------|-----------|
| A | no-caps モード + `\A` スキップ | `match?` で caps 配列の malloc を完全排除 | 全パターンの基礎コスト | — |
| B | literal prefix `memmem` | NFA を動かす前にリテラル先頭を `memmem` で検索、開始位置をジャンプ | literal / bounded | non-match 即 return |
| C | CHAR_CLASS ランスキャン | 1スレッド状態のとき文字クラスの連続ランをまとめてスキャン | `[a-zA-Z0-9]+` | +148% |
| D | CODE ランスキャン | 固定バイトのε-next が単純なとき同様に連続ランをスキャン | `a+b` | +392% |
| E | **Thompson NFA bitset** | スレッドリストを廃止。全スレッドを `uint64_t` 1 本に圧縮、分岐が OR 演算 1 回 | ≤63 状態の全パターン | ambiguous +983% |
| F-1 | Lazy DFA (Ruby) | NFA 状態集合 → 次状態集合の遷移をハッシュキャッシュ。同じ状態集合への再計算をゼロに | assertion-free 全パターン | YJIT で +100% |
| F-2 | Lazy DFA (C) | bitset に `(mask, byte) → next_mask` のキャッシュを追加。bitset 演算を繰り返さない | ≤63 状態の全パターン | ambiguous +154% |
| G-1 | 交替形 multi-memmem | 全枝がリテラルの交替形は `memmem` を全枝に並走させ最左位置を先取り | alternation non-match | 即 return |
| G-2 | 必須バイト prefilter | 「どのマッチにも必ず登場する ASCII バイト」を `memchr` で先行確認。なければ即 return | repetition non-match | +33% |
| G-3 | Lazy DFA cache 拡大 | 256→1024 スロット + 75% 充填でリセット。キャッシュヒット率向上 | `(a\|a)+b` | +37% |
| H | ascii_lookup 平坦テーブル | char_class の ASCII 判定を bitset 演算 → 配列 1 ロードに変更。SIMD 自動ベクタライズ | char_class スキャン全般 | 定数倍 ↓ |
| G-4 | YJIT 計測 | LazyDFA+YJIT で ambiguous が Onigmo+YJIT を 1.40x 超えを確認 | — | — |
| I | **リテラル VM バイパス** | 純リテラルは `memmem` の結果がそのまま答え。NFA を一切走らせない | `Watson` 等 | +14% |
| J | **交替形 VM バイパス** | 純 ASCII 交替形は alt pre-scan の最左位置がそのまま答え | `foo\|bar\|baz` | +25% |
| K | **非 ASCII VM バイパス** | 非 ASCII リテラル・交替形も `memmem` バイパスに対応 | `ジョバンニ\|カムパネルラ` | +36% |
| L | **first-byte table jump** | コンパイル時に「有効な先頭バイト集合」を 128 エントリ表で事前計算。NFA 初期状態で候補外バイトをバッチスキップ | bounded non-match | non-match ×2.7 vs Onigmo |
| M | **char-class ループバイパス** | `[X]+`/`\w+`/`\d+` は `ascii_lookup` を直接スキャンして NFA を完全排除 | char_class | 0.82x → 0.90x vs Onigmo |

### 10.3 ベンチマーク結果

> 測定: `bin/mruby tools/bench_pike_vm.rb`、Dev Container (x86_64)、2026-06-13。
> **combined** = match + non-match 混合バッチ。mruby 呼び出しのオーバーヘッドが
> 一定量含まれるため絶対値は変動しやすい。**non-match 単独**はエンジン本体の差が
> 最も直接的に現れる条件。

**累積改善 — combined (最適化前 → Cycle M 後):**

| パターン | 最適化前 (ips) | Cycle M 後 (ips) | 自己比 | Onigmo (ips) | Onigmo 比 |
|---------|-------------:|----------------:|------:|-------------:|----------:|
| `Watson` (literal) | 552 | ~2,540 | **4.6x** | 3,735 | 0.68x |
| `foo\|bar\|baz` (alternation) | 613 | ~2,300 | **3.8x** | 3,727 | 0.62x |
| `a+b` (repetition) | 51 | ~1,000 | **19.6x** | 1,721 | 0.58x |
| `(a\|a)+b` (ambiguous) | 17 | ~968 | **56.9x** | 719 | **1.35x** ↑ |
| `[a-zA-Z0-9]+` (char_class) | 675 | ~3,500 | **5.2x** | 3,901 | **0.90x** |
| `\d{4}-\d{2}-\d{2}` (bounded) | 501 | ~2,650 | **5.3x** | 3,549 | 0.75x |
| `ジョバンニ\|カムパネルラ` (unicode) | 586 | ~2,650 | **4.5x** | 3,657 | 0.72x |
| `(?:a?){30}a{30}` (pathological) | 10 | 10 | 1.0x | 808 | 0.01x |

**non-match 単独 — NFA バイパスの効果が最も顕著な条件:**

| パターン | non-match 入力 | Naraku (ips) | Onigmo (ips) | Naraku/Onigmo |
|---------|-------------|------------:|------------:|-------------:|
| `\d{4}-\d{2}-\d{2}` | `not-a-date` (digit なし) | **~9,540** | 3,549 | **×2.7** ↑ |
| `[a-zA-Z0-9]+` | `!!!` (alnum なし) | **~10,117** | 3,901 | **×2.6** ↑ |

**注目ポイント:**
- `ambiguous: (a|a)+b` — Lazy DFA + キャッシュ(E/F-2/G-3)で Onigmo 比 **1.35x**。バックトラッキングの指数的な分岐数を O(1) に圧縮。
- `bounded`/`char_class` の non-match — first-byte table(L) と ascii_lookup バイパス(M)の相乗で **Onigmo の 2.6〜2.7x**。NFA を動かさずにスキャン終了。
- `pathological` — 64 状態超のため bitset 最適化は未適用。それでも O(n) で完走(ReDoS 耐性は設計上保証)。

### 10.4 コミット構造

各サイクルの `perf:` コミットが最新ベンチを反映した状態で確定するよう、
`docs:` コミットは末尾 1 本にまとめる。

```
[コア実装]  feat: compiler / executor / bindings / tests
[Cycle A]   perf: match? no-capture + \A anchor skip
[Cycle B]   perf: literal prefix memmem pre-scan
[Cycle C]   perf: CHAR_CLASS ASCII run scan
[Cycle D]   perf: CODE ASCII run scan
[Cycle E]   perf: Thompson NFA bitset
[Cycle F-1] perf: Lazy DFA (Ruby)
[Cycle F-2] perf: Lazy DFA (C bitset VM)
[Cycle G-3] perf: Lazy DFA cache 1024 slots + reset-on-75%-full
[Cycle G-2] perf: required-byte prefilter (memchr early exit)
[Cycle G-1] perf: multi-literal alternation pre-scan
[Cycle H]   perf: ascii_lookup flat table for char-class scan
[Cycle G-4] bench: YJIT benchmark results (CRuby 4.0.5 --yjit)
[Cycle I]   perf: pure literal VM bypass
[Cycle J]   perf: pure alternation VM bypass
[Cycle K]   perf: non-ASCII literal/alternation VM bypass
[Cycle L]   perf: first-byte table jump for bitset path
[Cycle M]   perf: pure char-class loop VM bypass
[設計書]    docs: regex-vm.md                   ← 全体を総括
```

---

## 11. フェーズ進捗チェックリスト

| フェーズ | 内容 | 状態 |
|---|---|---|
| Phase 1 | C VM コア(コンパイラ + 実行器) | 🟩 完成 |
| Phase 2 | mruby マッチ API + Mtest テスト + ASan green | 🟩 完成 |
| Phase 3 | リリース整備(NARAKU_VERSION・CHANGELOG・README・`v0.1.0-alpha` タグ) | ⬜ スコープ外 |

- [x] エラーコード(-600 番台) + メッセージ
- [x] 公開ヘッダ `include/naraku_regex.h`
- [x] 設計ドキュメント(本書)
- [x] `src/regex_compile.c` 実装(2,234 行)
- [x] `src/regex_compile.c` の厳格フラグ(`-Wall -Werror -Wconversion` 等)でのビルド検証
- [x] `src/regex_vm.c`(`nk_program_search`)実装(1,142 行)
- [x] mruby バインディング(`mrb_naraku_program.c` + `regexp.rb`)
- [x] Mtest マッチングテスト(46 テスト — リテラル・量指定子・キャプチャ・文字クラス・アンカー・UTF-8)
- [x] ASan green(leak のみ、メモリ安全エラーなし)
- [x] `/i` フラグ — ASCII-only fold(`NK_FOLD_ASCII_ONLY`)・Simple fold(`NK_FOLD_DEFAULT`)・Full fold(`NK_FOLD_FULL`)・Turkish/Azeri fold(`NK_FOLD_TURKISH_AZERI`)全対応
- [x] `size_t` アンダーフローバグ修正(`code_in_code_range`、`\b` 誤判定の原因)
- [x] 対話型テスター `tools/match.rb`
- [x] 3 エンジン比較ベンチマーク `ruby-prototype/benchmark/bench_compare.rb`

**E2E 動作確認**:
```
$ bin/mruby tools/match.rb 'a(b|c)+d' 'xabcbd'
  pattern : a(b|c)+d
  subject : xabcbd
  match   : "abcbd"  [1...6]
  [1]     : "b"
  pre     : "x"
  post    : ""

$ bin/mruby tools/match.rb '(?=foo)' 'foobar'
  compile error: lookaround assertions are not supported in this version (at span 0...7)
```
