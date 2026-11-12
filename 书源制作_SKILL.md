---
name: "书源制作"
description: "制作并修复 Legado（阅读 App）书源/漫画源。当用户要求为小说或漫画网站制作书源，或修复搜索、目录、正文、发现页、图片加载等问题时调用。"
---

# Legado 书源制作

为「阅读」（Legado，`io.legado.app`）制作或修复书源 JSON。覆盖文本源（`bookSourceType:0`）、有声源（1）、漫画源（2）、视频源（3）。本技能语法以官方教程为准（书源规则 by 喵公子）。

## 何时使用

- 用户给出小说/漫画网址，说「制作书源 / 做源 / 制作漫画源」。
- 用户反馈「搜索不到 / 正文空白 / 目录顺序反了 / 图片加载不出 / 发现页不好看」。
- 用户丢来书源 JSON，说「修复 / 改一下」。

## 工作流（先抓页面，再写规则，严禁凭空编选择器）

1. **判断类型与编码**：`curl -sL -D- <url>` 看 `Content-Type` 的 `charset`。GBK 站本地调试用 `iconv -f GBK -t UTF-8//IGNORE`；HTTP 与 HTTPS 都测，部分站 HTTPS 会连接失败（如 comicbz）。
2. **抓关键页**：首页/分类页、搜索结果页、详情页、目录页、正文/图片页。带浏览器 UA，必要时加 `Referer`。
3. **验选择器**：`python3` + `BeautifulSoup` 验证命中；书源用与 Jsoup 一致语法。
4. **写 JSON**：输出到 `/workspace/<站点>.json`。
5. **验证**：`python3 -c "import json;json.load(open(...))"` 保证合法；关键 JS 逻辑本地 Python 模拟。

## 书源 JSON 核心字段

```json
{
  "bookSourceName": "站点名",
  "bookSourceType": 0,          // 必填: 0文本 1音频 2漫画 3视频
  "bookSourceUrl": "https://m.example.com", // 必填,唯一标识,重复会覆盖
  "bookUrlPattern": "https?://m\\.example\\.com/book/\\d+", // 识别源用,可不填
  "header": "{\"User-Agent\":\"...\"}",
  "searchUrl": "https://m.example.com/search?q={{key}}&page={{page}}",
  "exploreUrl": "分类::URL\n分类2::URL2",
  "jsLib": "function myUtil(){...}",        // 可选: 共享 JS,所有 @js 规则自动加载(放公共函数/常量)
  "loginUrl": "...", "loginUi": "...",      // 可选: 自定义登录(登录逻辑/登录界面,见高级特性)
  "eventListener": true,                     // 可选: 启用事件回调(配合 ruleContent.callBackJs)
  "ruleSearch":  {"bookList":"...","name":"...","author":"...","kind":"...","wordCount":"...","lastChapter":"...","intro":"...","coverUrl":"...","bookUrl":"...","checkKeyWord":"..."},
  "ruleBookInfo":{"init":"...","name":"...","author":"...","kind":"...","wordCount":"...","intro":"...","lastChapter":"...","coverUrl":"...","tocUrl":"..."},
  "ruleToc":     {"chapterList":"...","chapterName":"...","chapterUrl":"...","nextTocUrl":"...","isVip":"...","updateTime":"..."},
  "ruleContent": {"content":"...","nextContentUrl":"...","sourceRegex":"...","webJs":"...","callBackJs":"..."},
  "enabled": true, "enabledExplore": true, "enabledCookieJar": true
}
```

搜索/发现中 `{{key}}`=关键字、`{{page}}`=页码（初值1）。计算型用双大括号 `{{(page-1)*20}}`；首页无页号用 `{{page - 1 == 0 ? "" : page}}` 或 `<,{{page}}>`。`key` 可加密：`{{java.base64Encode(key)}}`、`{{java.md5Encode(key)}}`。

## 语法说明（以教程为准）

### 规则引擎（前缀决定解析方式）

| 引擎 | 前缀 | 说明 |
|---|---|---|
| Default(Jsoup) | 无（如 `class.xx.0@tag.a@text`） | 由 `@` 分段 |
| CSS | `@css:` | Jsoup CSS 选择器 |
| JSONPath | `@json:` 或 `$.` | Jayway JSONPath |
| XPath | `@XPath:` 或 `//` | JsoupXpath 1.0 |
| JavaScript | `@js:` / `<js></js>` | 见下 |

### Default 规则（`@` 分隔）
- 每段 3 部分：`类型.名称.位置`。类型含 `class`/`id`/`tag`/`text`/`children`（children 只需第一段）。
- 位置 0 起，负数为倒数（`-1` 最后）；不写位置=取全部。
- `!` 排除：`!0:2` 排除第1、3个。
- 列表前加 `-` 倒序（目录反转）。数组写法 `[i,j]`、`[!i,j]`、`[start:end:step]`、`tag.div[-1:0]` 反向。
- 末段取内容：`text`/`textNodes`/`ownText`/`href`/`src`/`html`/`all`。
- 正则替换接在最后：`##正则##替换`（替换空可省第二个 `##`）。
- 例：`class.odd.0@tag.a.0@text`、`head@[1]@text`（等价 `head@children[1]@text`）。

### 连接符号（仅限同类规则，不含 js/正则）
`&&` 合并所有值；`||` 取第一个非空；`%%` 逐项交替取值。

### 三种正则
- **AllInOne**（仅搜索/发现/详情预加载/目录列表）：必须以 `:` 开头，用 `$1`/`$2` 捕获，如 `:href="(/read[^"]*html)">([^<]*)`；加 `-` 前缀可同时倒序（`-:<li><a...`）。
- **OnlyOne**（列表之外规则）：`##正则##替换###`（末尾三个 `#`），只取第一个匹配。
- **净化**：`##正则##替换`，循环匹配替换；可跟在其它规则后，独立使用等价 `all##正则##替换`。

### JavaScript（`@js:` / `<js></js>`）
- 结果存入 `result`。`@js:` 只能放在规则**最后**；`<js></js>` 可任意位置，甚至作分隔符 `tag.li<js></js>//a`。
- ⚠️ **返回值用末尾裸表达式，不要写顶层 `return`**（顶层 `return` 会报 `返回的值无效`）。需要显式 `return`（如 try/catch、提前返回）时，用 IIFE 包裹：

```js
@js:(function(result){
  try { return org.jsoup.Jsoup.parse(result).select('xxx').first().html(); }
  catch(e){ return ""+e; }   // 显示报错信息
})(result);
```

- Jsoup 可直接用：`org.jsoup.Jsoup.parse(result)`、`selectFirst/select/.first()/.text()/.html()/.attr()`。

## 各模块规则

### URL 参数（GET/POST，拼在 URL 后的 `,{...}`）
```json
"searchUrl": "/search/,{\"method\":\"POST\",\"charset\":\"gbk\",\"body\":\"searchkey={{key}}\",\"webView\":true}"
```
参数：`method`、`charset`(utf-8 可省)、`headers`、`body`(POST 体，String 类型)、`webView`(非空则 webView 加载)、`type`、`js`(访问时执行)、`retry`(重试次数)。
复杂情况用 js 拼：`"https://x.com," + JSON.stringify(option)`。

### 搜索（ruleSearch）
字段同 JSON 结构。相对 URL 受支持。JSON API 用 `$.results.list`、`$.author[0].name` 等。
- `checkKeyWord`：关键字校验/清理 URL（命中即提示「删书示例」）。
- `bookList` 可 `<js>...</js>$[*]`：先跑 JS 拼 JSON 数组，再 JSONPath `$[*]` 逐条取。
- 规则内可套模板取上一步 JSON 字段：`bookUrl` 写 `/detail?book_id={{$.book_id}}`。

### 发现（exploreUrl）
- 整体可为 `@js:`：JS 动态生成发现页结构（可 `eval(String(source.loginUrl))` 初始化、`saveKeys()` 写入参数、返回对象/数组）。
- **格式一**：`名称::URL`，用 `&&` 或换行 `\n` 分隔。
- **格式二**：JSON 数组，每条 `{title,url,style}`，5 个样式属性：
  - `layout_flexGrow`（伸缩比）
  - `layout_flexShrink`（收缩比）
  - `layout_alignSelf`（单元素对齐：auto/flex-start/center/…）
  - `layout_flexBasisPercent`（占父容器宽百分比，-1 默认）
  - `layout_wrapBefore`（true 则强制换行作行首）

  区块标题整行居中 + 按钮每行两个 的写法：
  ```json
  [
    {"title":"✣ 排行榜 ✣","url":"/paihangbang/","style":{"layout_flexBasisPercent":1,"layout_wrapBefore":true,"layout_alignSelf":"center"}},
    {"title":"总榜","url":"/paihangbang/","style":{"layout_flexBasisPercent":0.45}}
  ]
  ```
  下拉组合筛选：`{{参数名=默认:选项值:选项}}`，如 `theme={{theme=,全部::aiqing:爱情:maoxian:冒险}}`。

> 注意：空 URL 的 `名称::,{style}` style 不生效；区块标题也应给真实 URL。

### 详情（ruleBookInfo）
- 预处理字段名是 **`init`**（不是 bookInfoInit）：AllInOne 或 js（js 需返回 json 对象，各字段名再被 name/author/… 引用）；也可在 `init` 里 `eval(String(source.loginUrl))` 初始化登录态、`book.putCustomVariable("1")` 存自定义变量。
- `canReName` 控制是否用详情覆盖搜索书名/作者。
- `tocUrl` 只支持单个 URL。

### 目录（ruleToc）
- `chapterList` 首字符 `-` 反序。
- `isVip`：结果为 null/false/0/"" 视为非 VIP。
- `nextTocUrl` 目录下一页，支持单个 url 或数组，返回 `[]`/null/"" 停止。
- `updateTime` 可用 `@js:java.timeFormat(timestamp)`（→yyyy/MM/dd HH:mm）。
- **分卷**：`chapterList` 用 `@js:` 返回对象数组，每项 `{ChapterName, isVolume, chapterUrl, ChapterInfo}`；`isVolume:true` 为卷标题行，`ChapterInfo` 附在章节后（更新时间/字数等）。
- **自定义章节 URL scheme**：`chapterUrl` 可写 `data:item_id;base64,<base64(item_id)>,{"type":"...","info":"book_id#item_id"}`，把参数编码进伪协议，正文 `ruleContent` 再解码还原真实接口。

### 正文（ruleContent）
- `content`：CSS/JSONPath/XPath/`all`（整页）/`<js>result</js>`。
- **优先纯选择器**：正文已 SSR 时直接写 `class.xxx@text`、`.content@html`，不要无脑上 `@js:`。选择器能过就不要 JS，少一层兼容风险。
- **`@js:` 正文取不到源码（空内容）**：`content` 用 `@js:` 时 `result` 不一定是整页源码（常见为空），稳定写法是取 `src`：
  ```js
  @js:(function(){var html=String((typeof src!=='undefined'&&src)?src:result); ...})()
  ```
  但若 `org.jsoup` 在该用户版本不可用，仍会静默失败——所以能改纯选择器就改纯选择器。排查顺序：`src` 有正文 → 换选择器；选择器能命中 → 不再用 JS。
- `nextContentUrl`：翻页，支持单个 url 或数组。
- `sourceRegex`：音频/视频资源嗅探正则（一般 `.*\.(mp3|mp4).*`），正文填 `<js>result</js>`。
- 图片链接可附加 headers：`'<img src="'+src+","+JSON.stringify({headers:{...}})+'">'`。
- 净化正文用 book/chapter 属性：`##{{book.name+"正文卷"+title}}`、`##{{chapter.title+chapter.index}}`。

### webView 正确追加写法
章节/详情链接要 webView 加载时，**别写成 `tag.a@href,{"webView":true}`**，正确形式：
- `tag.a@href##$##{"webView":true}`
- `tag.a@href##$##,{"webView":true}`
- `{{@@tag.a@href}},{"webView":true}`
- `tag.a@href@js:result+',{"webView":true}'`

## 变量与 Java 函数（JS 内可用）

**变量**：`baseUrl`(当前url)、`result`(上一步结果)、`book`(书籍类)、`chapter`(目录章节类)、`title`(当前标题)、`src`(源码)、`cookie`、`cache`。取属性用 `book.name`、`chapter.title`、`chapter.index` 等。

**常用 Java 函数**（`java.` 前缀；调用 java 包用全局 `Packages`）：
- 网络：`java.ajax(url)`、`java.ajaxAll(urlList)`、`java.get(url,headers)`、`java.post(url,body,headers)`、`java.connect(url).raw().request().url()`（跟随重定向取最终 URL）
- 编码/加密：`java.base64Decode/Encode`、`java.md5Encode`、`java.md5Encode16`、`java.utf8ToGbk`、`java.hexDecodeToString`、`java.encodeURI(str,enc)`、`java.aesDecodeToString(str,key,transformation,iv)` 等 AES 系列
- 工具：`java.htmlFormat(str)`、`java.timeFormat(time)`、`java.timeFormatUTC(time,fmt,offset)`、`java.getCookie(tag,key)`、`java.getVerificationCode(url)`（识别验证码图）、`java.toast(msg)`（弹提示）、`java.log(msg)`（调试日志）
- 解析复用：`java.setContent(content,baseUrl)`、`java.getString(rule)`（规则或 JSONPath 取字符串，如 `java.getString("$..cover")`）、`java.getStringList(rule)`、`java.getElements(rule)`

## 高级特性（登录 / 事件 / jsLib / 全局对象）

### jsLib（共享 JS）
顶层 `jsLib` 存公共函数与常量，**每个 `@js:`/`<js>` 规则执行前自动加载**，规则内可直接调用其中定义的函数（如加签 `xGod()`、取值 `Get()`）。适合放：请求加签、编解码、请求封装等复用逻辑，避免每条规则重复。

### 自定义登录（loginUrl + loginUi）
- `loginUrl` 实际存的是 **JS 代码字符串**（不是 URL），在规则里用 `eval(String(source.loginUrl))` 执行以初始化登录态（如 `ck`、设备参数）。
- `loginUi` 返回按钮数组：`{name, type:"button", action:"函数名", style:{layout_flexBasisPercent:0.45}}`，`action` 是登录相关函数（如 `device_register('API',true)`、`login(...)`）。
- 配套：`source.getLoginInfoMap()` 取登录信息、`saveKeys(map)` 保存、`source.removeLoginHeader()` 清除登录头。

### 事件回调（eventListener + callBackJs）
`eventListener:true` 后，`ruleContent.callBackJs` 在事件触发时执行，用 `event` 变量判断：`addBookShelf`/`delBookShelf`（加/移书架）、`startRead`/`endRead`/`saveRead`（阅读进度）、`clickBookLabel` 等。常用于同步书架、上传进度、书评。

### 全局对象（JS 内可用）
- `source`：`source.getKey()`（源标识）、`source.loginUrl`、`source.getLoginInfoMap()`、`source.removeLoginHeader()`、`source.ajax/get`。
- `book` / `chapter`：`book.bookUrl`、`book.name`、`book.putCustomVariable(k)` / `book.getVariable(k)`（自定义变量，可跨规则存类型/频道）；`chapter.url/title/index`。
- `cache`：`cache.get(key)` / `cache.put(key,val)` / `cache.delete(key)`（key 常用 `java.md5Encode16(...)` 生成）。
- `cookie`：`cookie.getKey(domain, key)` 读指定域 cookie（如 `cookie.getKey("fanqienovel.com","sessionid")`）。

## 关键坑与最佳实践

1. **编码**：GBK 站最正规做法是 URL 参数 `,{"charset":"gbk"}`（别只赌响应头 charset）；JS 里可 `java.utf8ToGbk`。
2. **`@js:` 顶层 `return` 崩溃**：改为末尾裸表达式，或 IIFE `(function(){...return x;})()`。这是最常见翻车点。
3. **webJs 必须有非空返回值**：否则被认为「未执行成功」无限循环。（见漫画图片技能）
4. **发现页排版**：用格式二 + 5 个 flex 样式属性；区块标题给真实 URL。
5. **目录反转**：规则级 `-` 前缀最省事；AllInOne 用 `-:`。
6. **搜索接口失效**：接口可能走第三方聚合（如 `so.biqusoso.com`）返回 500 或密文，先 curl 实测再写；必要时用发现页分类兜底，明确告知用户搜索暂不可用。
7. **Referer/UA**：图片站必须带 `Referer`，否则 403。
8. **漫画 JS 动态渲染**：`{"webView":true}` + `webJs`（详见 `漫画源图片提取` 技能）。
9. **Next.js / SPA 站找接口**：搜索页 HTML 可能只是空壳（Suspense 骨架、无结果），真实数据走 `/api/...`。做法：从页面取 `/_next/static/chunks/*.js`，批量下载后 `grep '"/api/'`、`grep 'search\|catalog\|chapter'`，还原请求参数与返回结构。九霄中文即此模式：搜索 `/api/books?q=&limit=&page=`、目录 `/api/books/{id}/catalog?offset=&limit=`。
10. **JSON 接口三件套写法**：搜索 `bookList:"$[*]"` + `bookUrl:".../{{$.id}}"`；目录 `chapterList:"$.rows[*]"` + `chapterName:"$.title"`；章节 URL 里用 `book.bookUrl` 正则取 bookId 再拼 `/{chapterId}`（`result` 可能是对象也可能是 JSON 字符串，两种都兜底）。
11. **目录接口分页**：先测 `limit` 上限（常见 2000），用 `nextTocUrl` 的 `@js:` 按 `offset+rows.length < total` 自算下一页；不要只取第一页。
12. **同一站多路由/多语言**：`/sim/book/{id}` 可能是简体详情，`/book/{id}/{cid}` 才是阅读页，路径前缀不同且语言可能不同；确认哪条路径带简体正文再写死。
13. **正文空（ContentEmptyException）定位**：日志里详情、目录都成功、只有正文空 → 99% 是 `ruleContent.content` 的 JS 问题（`result` 空 / `org.jsoup` 不可用 / 选择器错）。优先退回纯选择器，用 curl + BeautifulSoup 或 `grep -o 'class="reader-paragraph"'` 验证段落容器。
14. **发现页区块标题的 URL**：必须指向真正含书单的列表页（如 `/latest`、`/category/x`），别指首页；首页往往没有列表结构，点进去是空的。

## 参考成品（完整书源，可直接复制）

### 成品 1 —— 漫画源：动态图片 JS 变量提取 + packed JS 解码（comicbz.com）

要点：正文页用 `result.match(/VAR=(…)/)` 提取 cid/mid/count/签名，带 Referer 请求 `chapterimage.ashx`，接口返回 `eval(function…)` packed JS，`new Function(r+";return d;")()` 解出图片数组，签名过期自动刷新。

```json
[
  {
    "bookSourceName": "漫画bz",
    "bookSourceType": 2,
    "bookSourceUrl": "http://www.comicbz.com/",
    "bookSourceGroup": "漫画",
    "header": "{\"User-Agent\":\"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\"}",
    "searchUrl": "http://www.comicbz.com/search?title={{key}}",
    "ruleSearch": {
      "bookList": "ul.mh-list li",
      "name": ".mh-item .title a@title",
      "bookUrl": ".mh-item .title a@href",
      "coverUrl": ".mh-item .mh-cover@src",
      "lastChapter": ".mh-item .chapter a@text"
    },
    "ruleBookInfo": {
      "name": ".detail-info-title@text",
      "author": ".detail-info-tip a@text",
      "coverUrl": ".detail-info-cover@src",
      "intro": ".detail-info-content@text",
      "lastChapter": ".detail-list-form-title .s a@text"
    },
    "ruleToc": {
      "chapterList": "-.detail-list-form-con a.detail-list-form-item",
      "chapterName": "@text",
      "chapterUrl": "@href"
    },
    "ruleContent": {
      "content": "@js:var cid=result.match(/MANGABZ_CID=(\\d+)/)[1];var mid=result.match(/MANGABZ_MID=(\\d+)/)[1];var count=parseInt(result.match(/MANGABZ_IMAGE_COUNT=(\\d+)/)[1]);var base=\"http://www.comicbz.com/chapterimage.ashx\";var ref=\"http://www.comicbz.com/m\"+cid+\"/\";var ua=\"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\";function get(u){try{var c=new Packages.java.net.URL(u).openConnection();c.setRequestProperty(\"User-Agent\",ua);c.setRequestProperty(\"Referer\",ref);var b=new Packages.java.io.BufferedReader(new Packages.java.io.InputStreamReader(c.getInputStream(),\"UTF-8\"));var s=\"\",l;while((l=b.readLine())!==null)s+=l+\"\\n\";b.close();return s;}catch(e){}try{return String(source.ajax(u,null));}catch(e){}try{return String(source.get(u,null).body());}catch(e){}throw new Error(\"no http\");}function freshSign(){var h=get(ref);return{sign:h.match(/MANGABZ_VIEWSIGN=\"([^\"]+)\"/)[1],dt:h.match(/MANGABZ_VIEWSIGN_DT=\"([^\"]+)\"/)[1]};}var sd=freshSign();var imgs=[];for(var i=1;i<=count;i+=2){var u=base+\"?cid=\"+cid+\"&page=\"+i+\"&key=&_cid=\"+cid+\"&_mid=\"+mid+\"&_dt=\"+encodeURIComponent(sd.dt)+\"&_sign=\"+sd.sign;var r=get(u);if(r.indexOf(\"eval(function\")<0){sd=freshSign();u=base+\"?cid=\"+cid+\"&page=\"+i+\"&key=&_cid=\"+cid+\"&_mid=\"+mid+\"&_dt=\"+encodeURIComponent(sd.dt)+\"&_sign=\"+sd.sign;r=get(u);}var d=new Function(r+\";return d;\")();if(d&&d.length){for(var j=0;j<d.length;j++){if(imgs.indexOf(d[j])<0)imgs.push('<img src=\"'+d[j]+'&type=1\">');}}}imgs.slice(0,count).join(\"\\n\");"
    },
    "enabled": true,
    "enabledExplore": false,
    "respondTime": 180000
  }
]
```

> 同结构的 `yymanhua.com` 仅差异：域名换 `yymanhua.com`、变量前缀 `MANGABZ_*` 换 `YYMANHUA_*`、详情 `kind` 用 `.detail-info-tip .item@text`。

### 成品 2 —— 小说源：meta og:* 提取 + 发现页格式二排版（libahao2.com）

要点：详情全用 `meta[property=og:novel:*]` 提取；`ruleContent` 用 Jsoup 取 `#chapterText` 并净化；发现页用格式二分区 + 分类按钮每行两个。

```json
{
  "bookSourceName": "篱笆好文学",
  "bookSourceType": 0,
  "bookSourceUrl": "https://m.libahao2.com",
  "bookUrlPattern": "https?://m\\.libahao2\\.com/book/\\d+_\\d+/?",
  "enabledExplore": true,
  "exploreUrl": "[{\"title\":\"✨ 篱笆好文学 · 发现 ✨\",\"url\":\"/\",\"style\":{\"layout_flexBasisPercent\":1,\"layout_wrapBefore\":true,\"layout_alignSelf\":\"center\"}},{\"title\":\"✣ 排行榜 ✣\",\"url\":\"/paihangbang/\",\"style\":{\"layout_flexBasisPercent\":1,\"layout_wrapBefore\":true,\"layout_alignSelf\":\"center\"}},{\"title\":\"总阅读榜\",\"url\":\"/paihangbang/\",\"style\":{\"layout_flexBasisPercent\":0.45}},{\"title\":\"周榜\",\"url\":\"/paihangbang/\",\"style\":{\"layout_flexBasisPercent\":0.45}},{\"title\":\"✣ 分类 ✣\",\"url\":\"/xuanhuan/\",\"style\":{\"layout_flexBasisPercent\":1,\"layout_wrapBefore\":true,\"layout_alignSelf\":\"center\"}},{\"title\":\"玄幻魔法\",\"url\":\"/xuanhuan/{{page}}/\",\"style\":{\"layout_flexBasisPercent\":0.45}},{\"title\":\"武侠修真\",\"url\":\"/xiuzhen/{{page}}/\",\"style\":{\"layout_flexBasisPercent\":0.45}}]",
  "header": "{\"User-Agent\":\"Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36\"}",
  "ruleBookInfo": {
    "author": "meta[property=og:novel:author]@content",
    "coverUrl": "img.mb-cover@src",
    "intro": "meta[property=og:description]@content",
    "kind": "meta[property=og:novel:category]@content",
    "lastChapter": "meta[property=og:novel:lastest_chapter_name]@content",
    "name": "meta[property=og:novel:book_name]@content"
  },
  "ruleContent": {
    "content": "@js:var doc = org.jsoup.Jsoup.parse(result);\nvar content = String(doc.select('#chapterText').html());\ncontent = content.replace(/<b style=\"color:#ff0000[^\"]*\"[^>]*>[^<]*<\\/b>/g, '');\ncontent = content.replace(/<a [^>]*rel=\"next\"[^>]*>[\\s\\S]*?<\\/a>/g, '');\ncontent;",
    "nextContentUrl": "nav.mr-pagination a.mr-next@href"
  },
  "ruleExplore": {
    "author": ".m-book-meta@text##·.*",
    "bookList": "a.m-book-row, a.m-rank-book",
    "bookUrl": "@href",
    "coverUrl": "img.m-cover@src",
    "intro": ".m-book-description@text",
    "name": ".m-book-title@text"
  },
  "ruleSearch": {
    "author": ".m-book-meta@text##·.*",
    "bookList": "a.m-book-row",
    "bookUrl": "@href",
    "coverUrl": "img.m-cover@src",
    "intro": ".m-book-description@text",
    "name": ".m-book-title@text"
  },
  "ruleToc": {
    "chapterList": "ol.mb-chapters.mb-catalog li a",
    "chapterName": "text",
    "chapterUrl": "href"
  },
  "searchUrl": "https://m.libahao2.com/sou?wd={{key}}"
}
```

### 成品 3 —— 小说源：GBK + 正文分页合并 + Jsoup 多策略选择器（m.wxgbq.cc）

要点：站点 GBK；`ruleBookInfo` 用 Jsoup 遍历 `dd.dd_box span` 按「作者：/分类：」前缀匹配；`tocUrl` 指向 `all.html` 全目录；`ruleContent` 取 `#chaptercontent` 去广告/页码并 `nextContentUrl` 正则只收 `_数字.html`；搜索站内接口失效故留空。

```json
{
  "bookSourceName": "文学馆",
  "bookSourceType": 0,
  "bookSourceUrl": "https://m.wxgbq.cc",
  "bookSourceGroup": "小说",
  "bookUrlPattern": "https?://m\\.wxgbq\\.cc/wenzhang/\\d+/\\d+/",
  "enabledExplore": true,
  "searchUrl": "",
  "exploreUrl": "[{\"title\":\"✨ 文学馆 · 发现 ✨\",\"url\":\"/\",\"style\":{\"layout_flexBasisPercent\":1,\"layout_wrapBefore\":true,\"layout_alignSelf\":\"center\"}},{\"title\":\"✣ 热门 ✣\",\"url\":\"/\",\"style\":{\"layout_flexBasisPercent\":1,\"layout_wrapBefore\":true,\"layout_alignSelf\":\"center\"}},{\"title\":\"首页热榜\",\"url\":\"/\",\"style\":{\"layout_flexBasisPercent\":0.32}},{\"title\":\"全本小说\",\"url\":\"/quanben/\",\"style\":{\"layout_flexBasisPercent\":0.32}},{\"title\":\"排行榜\",\"url\":\"/wxgph/\",\"style\":{\"layout_flexBasisPercent\":0.32}},{\"title\":\"✣ 分类 ✣\",\"url\":\"/sort/1/1.html\",\"style\":{\"layout_flexBasisPercent\":1,\"layout_wrapBefore\":true,\"layout_alignSelf\":\"center\"}},{\"title\":\"玄幻\",\"url\":\"/sort/1/1.html\",\"style\":{\"layout_flexBasisPercent\":0.45}},{\"title\":\"仙侠\",\"url\":\"/sort/2/1.html\",\"style\":{\"layout_flexBasisPercent\":0.45}},{\"title\":\"都市\",\"url\":\"/sort/3/1.html\",\"style\":{\"layout_flexBasisPercent\":0.45}}]",
  "ruleExplore": {
    "bookList": ".hot .item, .rank .block ul.topli li, .block .lis li",
    "bookUrl": "@js:var a=result.select('a[href*=\"/wenzhang/\"]'); if(!a.isEmpty()) return a.first().attr('href'); return '';",
    "name": "@js:var n=result.select('dl dt a'); if(!n.isEmpty()) return n.first().text(); var m=result.select('.s2 a'); if(!m.isEmpty()) return m.first().text(); return '';",
    "author": "@js:var n=result.select('dl dt span'); if(!n.isEmpty()) return n.first().text(); return '';",
    "coverUrl": "@js:var i=result.select('div.image img'); if(i.isEmpty()) i=result.select('img'); return i.isEmpty()?'':i.first().attr('src');",
    "intro": "@js:var d=result.select('dl dd'); return d.isEmpty()?'':d.first().text();"
  },
  "ruleBookInfo": {
    "name": "dt.name@text",
    "author": "@js:var d=org.jsoup.Jsoup.parse(result); var s=d.select('dd.dd_box span'); for(var i=0;i<s.size();i++){var t=s.get(i).text(); if(t.indexOf('作者：')==0) return t.substring(3);} return '';",
    "kind": "@js:var d=org.jsoup.Jsoup.parse(result); var s=d.select('dd.dd_box span'); for(var i=0;i<s.size();i++){var t=s.get(i).text(); if(t.indexOf('分类：')==0) return t.substring(3);} return '';",
    "coverUrl": ".book_info .cover img@src",
    "intro": ".book_about dd@text",
    "lastChapter": ".book_last dd:eq(0) a@text",
    "tocUrl": "@js:baseUrl + 'all.html'"
  },
  "ruleToc": {
    "chapterList": ".book_last dd a[href*=\"/wenzhang/\"]",
    "chapterName": "@text",
    "chapterUrl": "@href"
  },
  "ruleContent": {
    "content": "@js:var doc=org.jsoup.Jsoup.parse(result); var e=doc.selectFirst('#chaptercontent'); if(e==null) return result; e.select('script').remove(); var h=e.html(); h=h.replace(/^[\\s\\S]*?<br\\s*\\/?>/i,''); h=h.replace(/第\\(\\d+\\/\\d+\\)页/g,''); h=h.replace(/\\(本章未完,请翻页\\)/g,''); h=h.replace(/文学馆阅读网址：m\\.wxgbq\\.cc/g,''); h;",
    "nextContentUrl": "@js:var d=org.jsoup.Jsoup.parse(result); var a=d.selectFirst('#pb_next'); if(a==null) return ''; var h=a.attr('href'); return /_\\d+\\.html$/.test(h) ? h : '';"
  }
}
```

> ⚠️ 成品 3 的 `ruleExplore`/`ruleBookInfo`/`ruleContent` 使用了顶层 `return`（早期作品）。按本技能「`@js:` 禁用顶层 `return`」规范，严格版本应改成末尾裸表达式或 IIFE 包裹；此处保留原样仅忠实于历史成品。

### 成品 4 —— 小说源：Jsoup 取正文 + nextContentUrl 续读（wap.jjwxc.net）

要点：GBK 搜索需手动 `gbkEncode(key)`；正文容器不止一个（`div.module ul.content_ul` → `ul.content_ul` → `#novelbody`）逐级兜底；`nextContentUrl` 遍历 `<a>` 找「下一章」文本。`@js:` 返回值一律末尾裸表达式，非顶层 `return`。

```json
{
  "bookSourceName": "晋江文学城",
  "bookSourceType": 0,
  "bookSourceUrl": "https://wap.jjwxc.net",
  "bookUrlPattern": "https://wap\\.jjwxc\\.net/book2/\\d+/?",
  "exploreUrl": "古言::https://wap.jjwxc.net/channel/index/gy\n现言::https://wap.jjwxc.net/channel/index/xy\n纯爱::https://wap.jjwxc.net/channel/index/dm",
  "header": "{\"User-Agent\":\"Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36\"}",
  "ruleBookInfo": {
    "author": "li.authorname-content@text##作者：",
    "intro": "#novelintro@text##简介：",
    "kind": "li:contains(类型)@text##类型：",
    "name": "meta[name=keywords]@content##^《|》,.*$"
  },
  "ruleContent": {
    "content": "@js:var doc = org.jsoup.Jsoup.parse(result);\nvar ul = doc.selectFirst('div.module ul.content_ul');\nif (ul == null) ul = doc.selectFirst('ul.content_ul');\nif (ul == null) ul = doc.selectFirst('#novelbody');\nvar out = '本章内容无法显示（可能已被下架、需登录或需购买）';\nif (ul != null) out = String(ul.html());\nout;",
    "nextContentUrl": "@js:var doc = org.jsoup.Jsoup.parse(result);\nvar nextUrl = '';\nvar links = doc.select('a');\nfor (var i = 0; i < links.size(); i++) {\n    var a = links.get(i);\n    if (a.text() == '下一章') {\n        nextUrl = a.attr('href');\n        break;\n    }\n}\nnextUrl;"
  },
  "ruleExplore": {
    "bookList": "div.module li a[href*=/book2/]",
    "bookUrl": "@href",
    "name": "@text"
  },
  "ruleSearch": {
    "author": "@text##.* - ",
    "bookList": "div.module li",
    "bookUrl": "a[href*=/book2/]@href",
    "name": "a[href*=/book2/]@text"
  },
  "ruleToc": {
    "chapterList": "@js:var doc = org.jsoup.Jsoup.parse(result);\nvar box = doc.select('#chapter_list_box');\nvar chapters = [];\nif (box.size() > 0) {\n    var links = box.first().select('a[href*=/book2/], a[href*=/vip/]');\n    for (var i = 0; i < links.size(); i++) {\n        var a = links.get(i);\n        var text = a.text();\n        var href = a.attr('href');\n        if (text.indexOf('展开') < 0 && text.indexOf('收起') < 0 && href.indexOf('javascript') < 0) {\n            chapters.push([text, href]);\n        }\n    }\n}\nchapters;",
    "chapterName": "@js:result[0]",
    "chapterUrl": "@js:result[1]"
  },
  "searchUrl": "@js:function gbkEncode(str){\n  var ret='';\n  for(var i=0;i<str.length;i++){\n    var code=str.charCodeAt(i);\n    if(code<0x80){\n      ret+=str.charAt(i);\n    }else{\n      try{\n        var bytes=java.lang.String(str.charAt(i)).getBytes('GBK');\n        for(var j=0;j<bytes.length;j++){\n          var b=bytes[j]&0xff;\n          ret+='%'+(b<16?'0':'')+b.toString(16).toUpperCase();\n        }\n      }catch(e){ ret+='%26'; }\n    }\n  }\n  return ret;\n}\nvar kw=gbkEncode(key);\n'https://wap.jjwxc.net/search?kw='+kw+'&version=1';"
}
```

### 成品 5 —— 漫画源：webView 懒加载图片（copy4000.com）

细节见 `漫画源图片提取` 技能，内含完整内嵌范例。

### 成品 6 —— 高阶范本：番茄（fanqienovel / snssdk，需登录）

源超 64KB 不内嵌；其展现的高阶语法已整理进上方「高级特性」，要点速览：

- `jsLib` 共享加签/编解码/设备注册函数（`xGod` 加签、`device_register`、`okhttpPost/Get`、`md5/gzip` 等）
- `loginUrl` 存 JS 代码（非 URL）+ `loginUi` 按钮登录，规则里 `eval(String(source.loginUrl))` 初始化登录态
- `eventListener` + `callBackJs` 同步书架（`addBookShelf`/`delBookShelf`）、上传进度（`startRead`/`endRead`）
- 全局对象：`book.putCustomVariable/getVariable`、`cache.get/put/delete`、`cookie.getKey(domain,key)`、`source.getKey/getLoginInfoMap`
- `ruleToc` 分卷（`isVolume`/`ChapterInfo`）+ `data:item_id;base64` 自定义章节 URL scheme
- `ruleBookInfo.init` 预处理、`ruleSearch.checkKeyWord`、`<js></js>$[*]` 链式、`{{$.book_id}}` 模板取值

### 成品 7 —— JSON 接口型小说源：Next.js 站（jiuxiaozw.com）

要点：Next.js 页面 SSR，但搜索/目录走 `/api`；搜索 JSON 数组、目录 JSON 分页；详情用纯选择器；**正文用纯选择器**（曾因 `@js:` 取 `result` 为空报 `ContentEmptyException`，改 `class.reader-paragraph@text` 后正常）。

```json
{
  "bookSourceName": "九霄中文",
  "bookSourceType": 0,
  "bookSourceUrl": "https://jiuxiaozw.com",
  "bookSourceGroup": "小说",
  "bookUrlPattern": "https?://jiuxiaozw\\.com/(?:sim/)?book/[0-9a-f]{16,}",
  "enabled": true,
  "enabledExplore": true,
  "enabledCookieJar": true,
  "header": "{\"User-Agent\":\"Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36\"}",
  "searchUrl": "https://jiuxiaozw.com/api/books?q={{key}}&limit=20&page={{page}}",
  "exploreUrl": "[{\"title\":\"✣ 书库 ✣\",\"url\":\"https://jiuxiaozw.com/sim/latest?page={{page}}\",\"style\":{\"layout_flexBasisPercent\":1,\"layout_wrapBefore\":true,\"layout_alignSelf\":\"center\"}},{\"title\":\"最近更新\",\"url\":\"https://jiuxiaozw.com/sim/latest?page={{page}}\",\"style\":{\"layout_flexBasisPercent\":0.32}},{\"title\":\"新书\",\"url\":\"https://jiuxiaozw.com/sim/new?page={{page}}\",\"style\":{\"layout_flexBasisPercent\":0.32}},{\"title\":\"完本\",\"url\":\"https://jiuxiaozw.com/sim/completed?page={{page}}\",\"style\":{\"layout_flexBasisPercent\":0.32}},{\"title\":\"玄幻\",\"url\":\"https://jiuxiaozw.com/sim/category/fantasy?page={{page}}\",\"style\":{\"layout_flexBasisPercent\":0.32}},{\"title\":\"仙侠\",\"url\":\"https://jiuxiaozw.com/sim/category/xianxia?page={{page}}\",\"style\":{\"layout_flexBasisPercent\":0.32}}]",
  "ruleSearch": {
    "bookList": "$[*]",
    "name": "$.title",
    "author": "$.author",
    "kind": "$.category",
    "intro": "$.description",
    "coverUrl": "$.cover_image",
    "bookUrl": "https://jiuxiaozw.com/sim/book/{{$.id}}"
  },
  "ruleBookInfo": {
    "name": "h1@text",
    "author": ".book-meta a@text",
    "kind": ".book-meta span.text-gray-900@text",
    "intro": ".book-description@text",
    "lastChapter": ".book-latest-newest .book-latest-title@text",
    "coverUrl": ".book-hero-cover img@src",
    "tocUrl": "@js:(function(){var m=String(book.bookUrl).match(/book\\/([0-9a-f]+)/); return m ? 'https://jiuxiaozw.com/api/books/'+m[1]+'/catalog?offset=0&limit=2000' : book.bookUrl;})()"
  },
  "ruleToc": {
    "chapterList": "$.rows[*]",
    "chapterName": "$.title",
    "chapterUrl": "@js:(function(){var m=String(book.bookUrl).match(/book\\/([0-9a-f]+)/); var id=''; try{id=result.id||'';}catch(e){} if(!id){try{id=JSON.parse(String(result)).id;}catch(e2){}} return 'https://jiuxiaozw.com/book/'+m[1]+'/'+id;})()",
    "nextTocUrl": "@js:(function(){var d; try{d=JSON.parse(String(result));}catch(e){return '';} var next=(d.offset||0)+(d.rows?d.rows.length:0); if(next<(d.total||0)){var m=String(baseUrl).match(/^([^?]+)/); return m[1]+'?offset='+next+'&limit=2000';} return '';})()"
  },
  "ruleContent": {
    "content": "class.reader-paragraph@text"
  },
  "ruleExplore": {
    "bookList": "li.book-list-row, li.ranking-row",
    "name": "h2@text",
    "author": "@js:(function(){var d=org.jsoup.Jsoup.parse(String(result));var a=d.select('.book-list-meta span');if(a.size()>0)return a.first().text();var b=d.select('.ranking-book-meta span');return b.size()>0?b.first().text():'';})()",
    "intro": "@js:(function(){var d=org.jsoup.Jsoup.parse(String(result));var a=d.select('.book-list-description');if(a.size()>0)return a.first().text();var b=d.select('.ranking-description');return b.size()>0?b.first().text():'';})()",
    "coverUrl": "img@src",
    "bookUrl": "a@href"
  }
}
```

> 目录超过 2000 章时靠 `nextTocUrl` 续页（实测 13022 章可完整加载）；章节 URL 的 `result` 同时兼容 JSON 对象与 JSON 字符串。

## 产出规范

- 一站点一 `.json`，放 `/workspace/`。
- `@js:` 规则末尾裸表达式，禁用顶层 `return`（或 IIFE 包裹）。
- 完成后用 `computer:///workspace/<文件>.json` 链接发给用户，提示「重新导入源验证」。
- **用户看不到 `computer://` 链接时**：直接把完整 JSON 贴在回复的代码块里让用户复制导入，不要只说“文件已生成”。
- 简明说明改了哪些规则、为什么；语法冲突以本教程为准。