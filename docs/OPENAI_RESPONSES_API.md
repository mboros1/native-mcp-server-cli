# OpenAI Responses API Reference

## POST `https://api.openai.com/v1/responses`

Creates a model response.  
Provide **text** or **image** inputs to generate **text** or **JSON** outputs.  
The model can also call your own **custom code** or built-in **tools** such as **web search** or **file search** to use your data as part of its reply.

---

### Request body

| Name | Type | Required | Default | Description |
| ---- | ---- | -------- | ------- | ----------- |
| `background` | `boolean \| null` | No | `false` | Run the response creation **asynchronously**. |
| `include` | `array \| null` | No | — | Extra data to embed in the response.<br>Supported values:<br>&nbsp;&nbsp;• `code_interpreter_call.outputs`<br>&nbsp;&nbsp;• `computer_call_output.output.image_url`<br>&nbsp;&nbsp;• `file_search_call.results`<br>&nbsp;&nbsp;• `message.input_image.image_url`<br>&nbsp;&nbsp;• `message.output_text.logprobs`<br>&nbsp;&nbsp;• `reasoning.encrypted_content` |
| `input` | `string` **or** `array` | No | — | One or more text, image, or file items that make up the **prompt**. |
| `instructions` | `string \| null` | No | — | A system/developer message injected into context. |
| `max_output_tokens` | `integer \| null` | No | — | Hard upper-bound on total tokens (visible + reasoning). |
| `max_tool_calls` | `integer \| null` | No | — | Total calls to built-in tools allowed in this response. |
| `metadata` | `map` | No | — | Up to **16 key-value** string pairs (64 char keys / 512 char values). |
| `model` | `string` | No | — | Model ID (e.g. `gpt-4o`, `o3`). |
| `parallel_tool_calls` | `boolean \| null` | No | `true` | Allow tool calls in parallel. |
| `previous_response_id` | `string \| null` | No | — | For multi-turn conversations; supplies prior response ID. |
| `prompt` | `object \| null` | No | — | Reference to a **prompt template** and its variables. |
| `prompt_cache_key` | `string` | No | — | Key used by OpenAI for prompt-level caching. |
| `reasoning` | `object \| null` | No | — | **o-series models only** – options for reasoning models.<br>• `effort`: `minimal`, `low`, `medium` (default), `high`<br>• `summary`: `auto`, `concise`, `detailed` *(replaces deprecated `generate_summary`)* |
| `safety_identifier` | `string` | No | — | Stable, hashed user identifier for policy compliance. |
| `service_tier` | `string \| null` | No | `auto` | Processing tier (`default`, `flex`, `priority`, or `auto`). |
| `store` | `boolean \| null` | No | `true` | Persist the response for later retrieval. |
| `stream` | `boolean \| null` | No | `false` | Stream results via **Server-Sent Events**. |
| `stream_options` | `object \| null` | No | `null` | Additional options when `stream` = `true`. |
| `temperature` | `number \| null` | No | `1` | Sampling temperature (0 – 2). |
| `text` | `object` | No | — | Formatting of the model's **visible** output.<br>• `format.type`: `text` (default), `json_object`, `json_schema` |
| `tool_choice` | `string \| object` | No | — | Force / constrain tool utilisation (`none`, `auto`, `required`, or a specific tool). |
| `tools` | `array` | No | — | Tools the model **may** call. Each entry is one of:<br>• **Built-in** tools – `web_search`, `file_search`, `code_interpreter`, `image_generation`, `computer_use`, etc.<br>• **Function** (custom code)<br>• **MCP tool** (remote Context-Protocol tool) |
| `top_logprobs` | `integer \| null` | No | — | Number (0-20) of top tokens to return with logprobs. |
| `top_p` | `number \| null` | No | — | Nucleus sampling parameter – alter **either** `temperature` **or** `top_p`, not both. |

---

### Nested structures

<details>
<summary><strong>`input` object variants</strong></summary>

| Variant | Type | Description |
| ------- | ---- | ----------- |
| **Text input** | `string` | Plain text equivalent to a `user` role message. |
| **Input item list** | `array` | Heterogeneous list of message / tool-call / reasoning items. |
| &nbsp;&nbsp;• `Input message` | `object` | `{ role: user \| assistant \| system \| developer, content, type="# message" }` |
| &nbsp;&nbsp;• `Item` | `object` | Text / image / audio context, previous assistant responses, tool call outputs, etc. |
| &nbsp;&nbsp;• `Item reference` | `object` | Internal reference ID to a prior item. |

</details>

<details>
<summary><strong>`text` → `format` options</strong></summary>

* `{ "type": "text" }` – default, unconstrained.  
* `{ "type": "json_schema", "schema": { … } }` – **Structured Outputs**; model must return JSON matching your schema.  
* `{ "type": "json_object" }` – legacy JSON-only mode (not recommended for `gpt-4o` or newer).  

</details>

<details>
<summary><strong>Tool objects</strong></summary>

* **Function tool** – define name, description, and JSON schema for arguments.  
* **File search tool** – enable retrieval-augmented generation from uploaded files.  
* **Web search tool** – search the public web for fresh context.  
* **Computer / Code interpreter / Image generation / Local shell / Custom / MCP tools** – see individual guides for schemas and constraints.  

</details>

---

### Example (minimal)

```json
POST https://api.openai.com/v1/responses
Content-Type: application/json
Authorization: Bearer $OPENAI_API_KEY

{
  "model": "gpt-4o",
  "input": "Write a haiku about autumn leaves.",
  "text": { "format": { "type": "text" } },
  "max_output_tokens": 256
}
```