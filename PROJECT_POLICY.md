# Noven public project policy / 公开项目政策

These permanent principles are the project owner's distribution commitments.
They do not replace the application code license or any independent third-party
license, and do not claim endorsement by authors or Battlestate Games.

## 1. Free access and noncommercial use / 免费与非商业使用

全部官方功能永久免费。不提供付费解锁、高级功能、付费订阅、强制捐款、付费地图、
付费官方插件访问或必须购买的使用许可。用户无需付款即可使用任何官方软件功能。
第三方许可证独立适用；使用第三方 NC 素材的实际行为保持非商业，不以“免费”或
“开源”自动替代对实际用途的判断。此政策不将第三方素材重新授权为 Noven 的许可。

All official functionality is free: no paid feature unlocks, premium features,
paid subscriptions, mandatory donations, paid map downloads, paid access to
official plugins or license fees required to use the application. Actual use of
NC-licensed materials remains noncommercial, not merely labelled free/open
source. Independent licenses retain their terms and ownership.

## 2. Respect authors and licenses / 尊重作者与许可

每项再分发素材在可访问文档及资源包内保留作者/贡献者、上游项目、原始来源 URL、
适用许可及链接、版权声明、本项目转换/修改及已提供的此前修改记录和第三方权利声明。
不冒称作者、不暗示背书、不以代码许可覆盖素材许可。缺失证据时明确记录实际缺口。

For every redistributed asset preserve creator identity, upstream project,
original source URL, applicable license and URL, required copyright notices,
Noven's modifications, supplied previous modification notices and relevant
third-party rights declarations. Notices remain accessible in documentation and
with resource packages. Do not fabricate provenance, replace original licenses,
claim authorship or imply endorsement.

## 3. No cheating / 绝对反作弊政策

核心、官方扩展/插件和官方 Registry/Marketplace 禁止游戏内存读写、游戏 DLL 注入、
为提取隐藏信息而抓取游戏封包、雷达/ESP、隐藏游戏状态提取、游戏自动化/pixel-bot、
绕过反作弊或意图提供被禁止的不公平优势的工具。官方审查不得明知而批准或推广此类插件。

The core, official extensions/plugins and official registry/marketplace prohibit
game memory access/modification, game DLL injection, game packet sniffing for
hidden information, radar/ESP, hidden-state extraction, gameplay automation or
pixel-bots, anti-cheat bypasses and tools intended to provide prohibited unfair
advantages. Official review must not knowingly approve or promote such plugins.

静态/交互地图、任务/撤离信息、公开游戏数据、用户可见屏幕 OCR、只读本地公开日志及
获授权的非作弊插件是合法伴侣功能的范围；这不扩大当前插件 API 的权限或数据边界。

Legitimate companion functions include static/interactive maps, quest/extraction
information, public game data, user-visible OCR, read-only local public logs and
authorized non-cheating plugins. This policy adds no plugin permissions or APIs.

PluginHost 只提供进程/崩溃隔离和受控 Noven API，不是 OS 沙箱。任意第三方原生 DLL
仍能以当前用户身份调用 Windows API；官方分发禁令不是对其技术能力的防作弊保证。

PluginHost is process/crash isolation, not an OS sandbox. Native DLLs can call
Windows APIs independently as the current user. Official distribution rules do
not make arbitrary third-party code technically incapable of cheating.

## 4. Shebuka SVG resources / SVG 素材适用范围

[Upstream](https://github.com/the-hideout/tarkov-dev-svg-maps) explicitly supports
community companion tools. Legitimate companion maps are not automatically
cheating. Apply its published [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/)
to qualifying SVG-derived material: verify exact source bytes/provenance,
preserve author/license/notices and conversion history, keep actual use
noncommercial and apply ShareAlike to copyrightable adaptations when required.
Preserve the upstream additional anti-cheating statement without misrepresenting
it as standard CC text. No extra written permission is required solely because
Noven is an Escape from Tarkov companion app.

SVG 许可不覆盖独立卫星图、游戏截图、第三方图标或未被授予的其他权利。逐包排除
未获授权的独立素材，不抹去其来源记录；不能把不完整楼层包称为完整地图。

The SVG license does not grant rights in unrelated satellite imagery,
screenshots or independently owned icons. Exclude uncleared material from each
package, retain its provenance records, and never label incomplete floors as a
complete map. See [map attribution evidence](docs/MAP_ATTRIBUTION.md).
