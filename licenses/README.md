# Runtime redistribution inventory / 运行资源再分发清单

核对日期：2026-10-09。本清单记录实际安装资源，不是“安全认证”或全部素材的授权证明。许可证及来源说明在 Program Root，绝不放入用户数据目录。地图授权缺口仍阻止公开发布。
Checked on 2026-10-09. This inventories installed resources, not a safety certification or blanket redistribution grant. Legal files belong to Program Root, not user data. Unresolved map rights still block public distribution.

## ONNX Runtime

- Component: Microsoft ONNX Runtime Windows x64 CPU, release **1.30.0**, DLL FileVersion `1.30.0.20260909.8.f2c39fe`.
- Exact source: [release](https://github.com/microsoft/onnxruntime/releases/tag/v1.30.0), commit `f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7`.
- DLL SHA-256: `7E39E2BDBBA836D98071EF28620735BA36A47C554CF794585269AECC50FAB0DA`.
- MIT: installed `licenses/onnxruntime/LICENSE`; matching complete upstream `licenses/onnxruntime/ThirdPartyNotices.txt` covers incorporated third-party notices. Original MIT compatibility copy remains `docs/LICENSE.onnxruntime.txt`.
- Exact notice source: [ThirdPartyNotices.txt at the release commit](https://raw.githubusercontent.com/microsoft/onnxruntime/f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7/ThirdPartyNotices.txt). This is not a notice from another version. Offline packaging tests pin its hash.
- Status: matching license/notice inclusion satisfied; binary identity is tied to its recorded version/hash, not a claim of signature verification or a rebuilt upstream binary.

## PaddlePaddle OCR models and dictionary

版权和模型来源为 PaddlePaddle/PaddleOCR，不属于 Noven 或 OpenAI。两个官方模型卡声明 Apache-2.0；随包保留完整 `licenses/paddleocr/LICENSE-2.0.txt`（[Apache 官方 2.0 原文](https://www.apache.org/licenses/LICENSE-2.0.txt)）及本来源/修改说明。所检查的官方模型文件树没有单独 NOTICE 文件；不虚构 NOTICE，也不借用其他版本的声明。如果权利人另提供适用声明，发布时须保留。
The models originate from PaddlePaddle/PaddleOCR, not Noven or OpenAI. Both official model cards declare Apache-2.0. Include the complete license and this source/modification attribution. The inspected official model trees have no separate NOTICE file; none is fabricated or borrowed from another version. Preserve any applicable upstream notice subsequently identified.

| Installed file | Exact identity / official source | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `assets/models/ppocrv5_mobile_det.onnx` | [PP-OCRv5 mobile detector ONNX](https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_det_onnx/blob/df0bd9dee2bc627e80a2a1798ccab35a332e22d6/inference.onnx) | 4826518 | `A431985659DC921974177A95ADCFBB90FD9E51989A5E04D70D0B75F597B6E61D` |
| `assets/models/ppocrv5_mobile_rec.onnx` | [PP-OCRv5 mobile recognizer ONNX](https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_rec_onnx/blob/ec1bd72732b2b5f52df0a66321403eebb520ba74/inference.onnx) | 16534782 | `DA72DC72CA4DC220DF0DFDE68C1DEDC31C58D3E76A25871122E5056227D50092` |
| `assets/models/ppocrv5_mobile_rec_dict.txt` | [Recognizer inference.yml](https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_rec_onnx/blob/ec1bd72732b2b5f52df0a66321403eebb520ba74/inference.yml), extracted dictionary | 92458 | `52B1233DD55164B2A7121D4531A01E362AAAA90905CAD6AC83508D9A2065B1F8` |

模型字节的 SHA-256 与官方文件页匹配；没有修改权重。字典来源沿用仓库已记录的官方 `inference.yml` 提取过程：UTF-8、18,383 项、一项一行（格式转换，非新模型）。本次远程 YAML 内容重新提取核验若不可用，不应把已有来源记录称为新独立核验。模型 Apache 许可副本及来源归属已打包，不声称软件许可消除训练数据或其他未授予权利。
Model SHA-256 values match the official file pages; weights are unmodified. Dictionary provenance follows the repository's recorded extraction from official inference.yml: UTF-8, 18,383 entries, one per line (format conversion, not a new model). If a fresh remote YAML reconstruction is unavailable, the existing provenance record is not a new independent verification. Apache license and source attribution are packaged, without implying a software license clears training-data or other ungranted rights.

## Maps, icons and generated catalogs

完整逐文件 URL、内容哈希、作者及转换记录仍保留在 `assets/maps/all.manifest.json`、各层/图标 manifest、`NOTICE.md`、`docs/MAP_ATTRIBUTION.md` 和 SOURCE 文件。源 PNG 的安装筛选不删除这些记录。
Per-file URLs, hashes, authors and transformations remain in the map manifests, NOTICE, MAP_ATTRIBUTION and SOURCE files. Source-PNG install selection does not remove provenance.

| Source group | Classification | License / required evidence | Current redistribution status |
| --- | --- | --- | --- |
| Shebuka SVG-derived layouts, previews and tiles | B: attribution and conditions required | CC BY-NC-SA 4.0; upstream additional use restriction; retain NOTICE/SOURCE/manifests, license link, modifications and disclaimers | Evidence recorded; actual distribution must satisfy noncommercial/share-alike and applicable conditions. Not an unconditional authorization for all map images. |
| Tarkov.dev satellite tiles / generated satellite packs (all maps) | C: unclear independent image permission | Hosting/layout authorship is not an image license; Battlestate Games / respective owners | **Public release blocked pending explicit applicable redistribution evidence.** |
| Tarkov.dev / TarkovBOT.eu non-SVG layouts or imagery | C unless a specific grant is recorded | Exact source ledger identifies content; a site or author label alone is insufficient | **Public release blocked for unresolved groups.** |
| Repository-sourced interactive icons | B for upstream repository contribution; C for unresolved game-image rights | MIT, Oskar Risberg 2019, commit `ef62766bbd7ffb294c7184f9b8bfe8ed0f18320e`; installed `assets/maps/icons/LICENSE.tarkov-dev.txt` and SOURCE | MIT notice retained; independent game/third-party image rights not established. |
| Handbook category icons | C | `regular/items` image URLs identify source, not a redistribution license | **Public release blocked pending image authorization.** |
| Noven code, projection/index generation and original metadata contributions | D: original contribution only | Does not replace underlying source/derivative asset terms | No ownership claim over third-party pixels or catalog content. |
| Generated Items/Hideout/Tasks/Maps TSV/JSON | Generated production data | Tarkov.dev API inputs and generation ledgers; retained game names/content belong to respective owners; no blanket asset license inferred | Source attribution retained; not a rights clearance for all upstream content. |

A 类（明确无额外条件的地图再分发授权）没有从现有证据确认；B 类也不能涵盖 C 类。需要用户/权利人补充适用的图像再分发授权，或者另行决定替换/排除对应素材。不能只用署名“解决”授权。
No unconditional category-A map grant has been established from current evidence. Category B does not cover C. Obtain applicable image permissions or separately decide to replace/exclude affected assets; attribution alone does not resolve this blocker.

## Microsoft VC++ prerequisite and Windows components

`vc_redist.x64.exe` is embedded in Setup only, not copied into Program Root. The reviewed prerequisite is Microsoft-signed **14.51.36247.0**, SHA-256 `843068991DAAA1F73AD9F6239BCE4D0F6A07A51F18C37EA2A867E9BECA71295C`, from Visual Studio's `VC/Redist/MSVC/14.51.36231` redistribution directory. Preserve the original signed installer/EULA and comply with the applicable Microsoft Visual Studio redistribution terms; this is not MIT. No loose CRT DLLs are bundled. Windows Win32/Direct2D/DirectWrite/WIC/WinHTTP are operating-system prerequisites, not redistributed Windows binaries.

没有打包 Python、PaddlePaddle 开发运行时、生成器依赖、测试 EXE 或 Demo DLL；ONNX 内嵌依赖声明以匹配的 ThirdPartyNotices 为准。
Python, the Paddle development runtime, generator dependencies, test executables and demo DLLs are not shipped. ONNX incorporated-dependency notices are in its matching ThirdPartyNotices.
