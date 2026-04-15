# HIR Specification (Draft)

この文書は、パース後の正規表現を前処理して HIR へ lowering し、HIR を実行する仕様を記録する。

## 1. Preprocess

パース後 AST に対して、次を順に実行する。

1. `GroupNameResolver`
   - 名前付きグループへグループ番号を設定する。
   - 同名グループの複数定義は許可する。
2. `GroupRefResolver`
   - 後方参照・条件グループ・部分式呼び出しの参照先を解決する。
   - 参照先は `cap_ids: Array[Int]`（候補配列）で保持する。
   - 深さ付き参照が使われるキャプチャを記録する。
3. `EmptyMatchAnalyzer`
   - 各ノードが空文字列にマッチ可能かを判定する。
4. `InfiniteCallAnalyzer`
   - nullable な遷移のみで call-cycle（相互再帰含む）へ到達可能なら静的エラー。
5. `EmptyLoopCapsAnalyzer`
   - 空ループ判定に必要なキャプチャ依存集合を計算する。
   - positive lookaround 内で更新されるキャプチャは依存集合から除外する。
6. `CharClassExpander`
   - `[...]`, `\w`, `\p{...}` などを interval 配列へ展開する。
7. `IgnoreCaseExpander`
   - `ignore_case` に従って unfold 展開する。
   - 展開後の `Inst::Class` は `ignore_case=false` 前提で扱う。
   - multi-char folding（例: `ß`）は `Class + Match` の `Alt` に分解して表現する。

## 2. HIR Structure

```c
struct HIR:
  entry: BlockId
  blocks: Map[BlockID, Block]

struct Block:
  insts: Array[Inst]
  term: Term
```

### 2.1 Inst

```c
enum Inst:
  case Match(lit: String, info: MatchMeta)
  case Dot(newline: Bool, dir: Dir)
  case Class(intervals: Array[Interval], dir: Dir)
  case Ref(cap_ids: Array[CapId], depth: Option[Int], info: MatchMeta)
  case Assert(assertion: Assertion)
  case CapBegin(cap_id: CapId)
  case CapEnd(cap_id: CapId, needs_depth: Bool, empty_check: Bool)
  case AtomicBegin
  case AtomicEnd
  case ResetReg(reg_id: RegId)
  case IncReg(reg_id: RegId)
  case SaveCapGen(loop_id: LoopId)
  case Memo(info: MemoInfo)
```

`MatchMeta` は次を持つ:

```c
struct MatchMeta:
  ignore_case: Bool
  fold: FoldFlag
  dir: Dir
```

### 2.2 Dir / Assertion

```c
enum Dir:
  case Forward
  case Backward

enum Assertion:
  case WordBoundary(dir: Dir)
  case NonWordBoundary(dir: Dir)
  case LineBegin
  case LineEnd
  case Begin
  case End
  case EndLoose
  case MatchBegin
```

### 2.3 Term / Trans / Cond

```c
enum Term:
  case Next(tr: Trans)
  case Call(sub: BlockId, ret: BlockId)
  case LookAround(positive: Bool, dir: Dir, sub: BlockId, to: BlockId)
  case Absence(sub: BlockId, to: BlockId)
  case Conditional(cap_nums: Array[Int], depth: Option[Int], yes: BranchId, no: BranchId)
  case End
  case Fail
  case EndLookAround
  case Return
  case EndAbsence

enum Trans:
  case Fork(cond: Array[Cond], to: BlockId, tr: Trans)
  case Jump(cond: Array[Cond], to: BlockId)

enum Cond:
  case LtReg(reg_id: RegId, n: Int)
  case CheckCapGen(loop_id: LoopId, cap_ids: Array[CapId])
```

## 3. Runtime Model

```c
struct HIRRuntime:
  pos: Int
  bt_stack: Stack[Bt]
  sub_stack: Stack[Sub]
  caps: Map[CapId, CapState]
  call_caps: Array[Map[CapId, CapState]]
  current_gen: Int
  loop_gen: Map[LoopId, Int]
  cap_gen: Map[CapId, Int]
```

```c
enum Bt:
  case Bt(pos: Int, tr: Trans)
  case UndoCap(cap_id: CapId, cap_state: CapState)
  case UndoReg(reg_id: RegId, value: Int)
  case PopCall
  case PushCall(ret: BlockId)
  case MarkLookAround
  case ProtectLookAround
  case MarkAbsence
  case MarkAtomic
  case ProtectAtomic
  case RestoreCapGen(loop_id: LoopId, prev_gen: Int)

enum Sub:
  case Call(ret: BlockId)
  case LookAround(pos: Int, positive: Bool, dir: Dir, to: BlockId)
  case Absence(pos: Int, sub: BlockId, to: BlockId)

enum CapState:
  case None
  case Partial(begin: Int)
  case Set(begin: Int, end: Int)
```

## 4. Memoization

### 4.1 可否判定

- 後方参照（depth 有無問わず）が到達可能な場合は **メモ化しない**。

### 4.2 キー

- 基本キーは `pos + memo_block_id + deps(regs)`。
- `loop_gen` はキーに含めない。
- 不要レジスタは含めない（block ごとの依存レジスタ集合を計算）。
- キーの有限化のため、各レジスタ `r` は `bound(r)`（参照される `LtReg` の最大閾値）で飽和:
  - `r' = min(r, bound(r))`
- シリアル化は mixed-radix で一意キーを作る:
  1. `base = pos * num_memo_blocks + memo_block_id`
  2. `deps` の順序固定で `base` に `r'` を畳み込む。

## 5. Notes

- `EmptyLoopCapsAnalyzer` で positive lookaround 内キャプチャを除外するため、空ループ判定の過剰振動を回避する。
- 同名グループ複数定義は許容し、参照は候補配列で保持する。
