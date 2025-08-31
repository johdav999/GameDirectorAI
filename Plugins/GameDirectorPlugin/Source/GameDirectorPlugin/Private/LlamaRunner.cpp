#include "LlamaRunner.h"
#include "ggml-cuda.h"
#include <vector>
#include <string>


#ifndef LLAMA_API_VERSION
#define LLAMA_API_VERSION 0
#endif

// ---- COMPAT HELPERS (do NOT expose llama_vocab outside) ----
