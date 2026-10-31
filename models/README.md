# The local model inside NIX

Core 0.2 runs **SmolLM2-135M-Instruct** on the guest CPU. The tokenizer,
transformer, generation loop, and response rendering execute inside Core.
The weights are an immutable Multiboot boot module loaded with the system.
The launcher verifies the bundle and attaches it automatically. No model server,
host inference process, API key, or guest network device is used.

## Model and provenance

- Upstream: [HuggingFaceTB/SmolLM2-135M-Instruct](https://huggingface.co/HuggingFaceTB/SmolLM2-135M-Instruct).
- Pinned revision: `12fd25f77366fa6b3b4b768ec3050bf629380bac`.
- License: [Apache 2.0](LICENSE-APACHE-2.0.txt). The [upstream model card](MODEL-CARD.md) is retained.
- Original weights: about 257 MiB in BF16 safetensors format.
- Core boot bundle: 138,070,032 bytes, about 132 MiB; Q8 row-quantized weights,
  byte-level BPE tokenizer data, and a fixed assistant prompt.
- [sources.json](sources.json) pins source-file SHA-256 hashes;
  [manifest.json](manifest.json) records the prepared bundle hash and size.

This directory tracks provenance and preparation instructions. The large source
weights and generated bundle are excluded from ordinary Git history. They live
locally after preparation and can be distributed with the system as a release
asset. Existing personal Memories remain on their separate disk.

## Prepare once, then run offline

The prepared development workspace already has the model downloaded and converted.
For a fresh checkout:

```sh
./scripts/setup-model.sh
./scripts/run.sh
```

Preparation downloads pinned sources, verifies their hashes, creates a local Python
virtual environment, installs NumPy and tokenizers, and produces the boot bundle.
Those Python dependencies are preparation tools. Inference inside Core does not
use Python, PyTorch, NumPy, an HTTP bridge, or the host's model runtime.

Downloads use safetensors rather than executing pickle payloads. If an existing
source file has the wrong checksum, the downloader stops instead of replacing it.
The runtime bundle is also hash-verified by the launcher before boot.

## Natural interface

From Home, type an ordinary question, such as “What is the capital of France?”
or “Explain how a computer saves a file.” The model streams a short answer into
the NIX interface. Prefix a question with `ask` to bypass deterministic intent
shortcuts, and use `ai status` to inspect the loaded model.

Press **Escape** to cancel inference. In a headless serial session, Escape or
Control-C cancels. The mouse remains serviced during matrix operations. Typing
other text during inference is ignored rather than queued as a later command.

Explicit OS commands continue through deterministic handlers. Model-generated
text is displayed as advice; it is never fed into Ghost, treated as a command,
or given authority to install software or alter Memories. Existing manuscripts
and their contents are not automatically inserted into the model prompt.

## Runtime architecture

The engine implements the model's 30 decoder layers, grouped-query attention,
split-half rotary embeddings, RMS normalization, SiLU feed-forward layers, tied
token embeddings, and greedy next-token generation. Weight and activation dot
products use signed 8-bit values with SSE2 integer accumulation; residual streams
and the attention cache use float32. The original BF16 weights are converted
rather than trained or fine-tuned by this project.

Core enables SSE2 before entering C. Model and tokenizer tables are loaded from
the boot module with size, dimension, offset, and shape checks. The model does not
need a general filesystem to start. The guest uses 512 MiB RAM, while existing
model-free regression boots still run with 64 MiB.

A 256-token total context and 48-token response cap bound memory and work.
The fixed system-prefix attention cache is reused across requests. Each user
request otherwise starts a fresh context: multi-turn chat history, retrieval from
Memories, and tool planning are not implemented yet. The interface accepts ASCII
input and displays printable ASCII and newlines from generated text.

## Capability and performance limits

This is a small instruction-tuned model. It can generate explanations and simple
answers, but can hallucinate, misunderstand requests, and give incorrect plans.
It does not establish reliable general reasoning or autonomous application building.
The OS enforces the separation between generated advice and executable actions.

An actual offline QEMU test on the development Intel Mac answered the France
capital question in about **12 seconds** with software CPU emulation after SSE2
optimization. That is one measured prompt, not a general latency guarantee;
longer requests and responses take more work. A second cached-prefix capital question took about **6 seconds** in the same guest. A previously tested scalar-float
path took about 71 seconds for the same prompt and was replaced.

## Verification

```sh
# Requires the prepared model and preparation Python dependencies:
models/.venv/bin/python tests/ai_reference.py
python3 tests/ai_guest.py

# Foundation regression suite; does not download or require model weights:
python3 tests/smoke.py
```

The reference test compares ASCII BPE against the upstream tokenizer and compares
inference with an independent NumPy transformer. The W8A8 validation also checks 1000 SIMD dot products against scalar sums, verifies the leading generated token, and measures logit distribution correlation; quantization rounding can produce different logits across implementations. The guest test boots with the
model attached and NIC absent, checks actual generated answers, exercises
cancellation, and verifies that the deterministic writing/save path still works.
