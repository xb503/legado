# -*- coding: utf-8 -*-
import json, io

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

CAP = "function __cap(h){return h.indexOf('GOEDGE_WAF_CAPTCHA_ID')>=0||h.indexOf('Verify Yourself')>=0;}"

search_js = r"""@js:(function(){
var UA='__UA__';
var base='https://www.rrssk.com/';
__CAP__
function __sig(t,ts){
  var e=encodeURIComponent(String(key)).replace(/[!'()*]/g,function(c){return '%'+c.charCodeAt(0).toString(16).toUpperCase();});
  var h=0;
  for(var i=0;i<e.length;i++){h=(h*15259+e.charCodeAt(i)+t.charCodeAt(i%t.length))>>>0;h=(h^2630)>>>0;}
  h=(h+ts)>>>0;
  for(var r=0;r<37861;r++){h=((h<<5)-h+21787)>>>0;}
  return ('00000000'+((h^297)>>>0).toString(16)).slice(-8);
}
function __body(t,ts){return '_t='+encodeURIComponent(t)+'&_ts='+ts+'&_s='+__sig(t,ts)+'&keyword='+encodeURIComponent(String(key));}
var gj=JSON.stringify({'User-Agent':UA,'Accept-Language':'zh-CN,zh;q=0.9'});
var pj=JSON.stringify({'User-Agent':UA,'Accept-Language':'zh-CN,zh;q=0.9','Referer':base+'00PB.html','Origin':'https://www.rrssk.com','Content-Type':'application/x-www-form-urlencoded'});
var html='';
try{html=String(java.get(base+'00PB.html',gj).body());}catch(e){}
if(__cap(html)||html.indexOf('name="_t"')<0){
  try{java.startBrowserAwait(base+'00PB.html','正在通过搜索验证，请稍候');}catch(e){}
  try{html=String(java.get(base+'00PB.html',gj).body());}catch(e){}
}
var m=html.match(/name="_t" value="([^"]+)"/);
if(!m){throw new Error('未取得搜索凭证：搜索站异常，请稍后重试');}
var t=m[1],ts=Math.floor(Date.now()/1000);
var loc='';
try{loc=java.post(base+'?action=search',__body(t,ts),pj).header('Location')||'';}catch(e){}
if(!loc){
  // POST 被 WAF 拦截(403)或失败：拉起浏览器静默建立 ge_wc_45 Cookie
  try{java.startBrowserAwait(base+'00PB.html','正在通过搜索验证，请稍候');}catch(e){}
  var h2='';
  try{h2=String(java.get(base+'00PB.html',gj).body());}catch(e){}
  var m2=h2.match(/name="_t" value="([^"]+)"/);
  if(m2){
    t=m2[1];ts=Math.floor(Date.now()/1000);
    try{loc=java.post(base+'?action=search',__body(t,ts),pj).header('Location')||'';}catch(e){}
  }
}
if(!loc){throw new Error('搜索被拦截(403)：搜索站 WAF 需浏览器验证，重试仍失败可能站点维护中，可先用“发现”阅读');}
if(loc.indexOf('/WAF/')>=0){throw new Error('需要人机验证：请重试搜索并完成验证');}
if(loc.indexOf('http')!==0){loc='https://www.rrssk.com'+loc;}
try{
  var mb=String(java.get('https://www.mopbook.com/',gj).body());
  if(__cap(mb)){java.startBrowserAwait('https://www.mopbook.com/','请完成猫扑的人机验证');}
}catch(e0){}
loc=loc.replace(/([?&])page=\d+/,'$1page='+page);
if(!/[?&]page=/.test(loc)){loc+=(loc.indexOf('?')>=0?'&':'?')+'page='+page;}
return loc;
})()""".replace("__UA__", UA).replace("__CAP__", CAP)

toc_js = r"""@js:(function(){
__CAP__
var UA='__UA__';
var u=String(book.bookUrl);
var bm=u.match(/book\/([0-9A-Z]+)\.html/);
var id=bm?bm[1]:'';
var html=String(result);
var hd=JSON.stringify({'User-Agent':UA});
if(__cap(html)||html.indexOf('og:novel')<0){
  try{java.startBrowserAwait(u,'请完成猫扑的人机验证');}catch(e){}
  try{html=String(java.get(u,hd).body());}catch(e){}
}
var pages=(html.match(/<option[^>]*value="\d+"/g)||[]).length;
if(pages<1)pages=1;
var body='data='+encodeURIComponent(java.createSymmetricCrypto('AES/CBC/PKCS5Padding','Hi3yccTfyPoxfoexMyZ2ohk0xFgjj1mN','2685853201487428').encryptBase64(JSON.stringify({id:id,page:1})));
return 'https://www.mopbook.com/index.php?action=loadChapterPage&page=1&pages='+pages+'&id='+id+',{"method":"POST","body":"'+body+'"}';
})()""".replace("__UA__", UA).replace("__CAP__", CAP)

next_toc_js = r"""@js:(function(){
var d;
try{d=(typeof result==='string')?JSON.parse(result):result;}catch(e){return '';}
var rows=null;
try{if(typeof d.get==='function'){rows=d.get('data');}else{rows=d.data;}}catch(e){try{rows=d.data;}catch(e2){return '';}}
if(rows==null)return '';
var n=rows.length;
if(n==null&&rows.size){try{n=rows.size();}catch(e){n=0;}}
if(!n||n<100)return '';
var m=String(baseUrl).match(/[?&]page=(\d+)/);
var p=m?parseInt(m[1]):1;
var pm=String(baseUrl).match(/[?&]pages=(\d+)/);
var pages=pm?parseInt(pm[1]):100;
if(p>=pages||p>=100)return '';
var next=p+1;
var im=String(baseUrl).match(/[?&]id=([0-9A-Z]+)/);
var id=im?im[1]:'';
if(!id){var bm=String(book.bookUrl).match(/book\/([0-9A-Z]+)\.html/);id=bm?bm[1]:'';}
var body='data='+encodeURIComponent(java.createSymmetricCrypto('AES/CBC/PKCS5Padding','Hi3yccTfyPoxfoexMyZ2ohk0xFgjj1mN','2685853201487428').encryptBase64(JSON.stringify({id:id,page:next})));
return 'https://www.mopbook.com/index.php?action=loadChapterPage&page='+next+'&pages='+pages+'&id='+id+',{"method":"POST","body":"'+body+'"}';
})()"""

bookurl_js = (r"""@js:var s=String(result);var m=s.match(/[?&]t=([^&'"]+)/);if(!m){return '';}"""
              r"""var b=decodeURIComponent(m[1]).replace(/-/g,'+').replace(/_/g,'/').replace(/[^A-Za-z0-9+/=]/g,'');"""
              r"""var c='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';var o='';var buf=0,bits=0,i,v;"""
              r"""for(i=0;i<b.length;i++){v=c.indexOf(b.charAt(i));if(v<0)continue;buf=(buf<<6)|v;bits+=6;"""
              r"""if(bits>=8){bits-=8;o+=String.fromCharCode((buf>>bits)&255);}}"""
              r"""return o;""")

probe_js = ("(function(){var h='';try{h=String(java.get('https://www.mopbook.com/',"
            "JSON.stringify({'User-Agent':'__UA__'})).body());}catch(e){}"
            "if(h.indexOf('GOEDGE_WAF_CAPTCHA_ID')>=0||h.indexOf('Verify Yourself')>=0){"
            "try{java.startBrowserAwait('https://www.mopbook.com/','请完成猫扑的人机验证');}catch(e0){}}"
            "return result;})()").replace("__UA__", UA)

cats = [
    ("男生", 1), ("女生", 2), ("二次元", 3), ("玄幻", 4), ("都市", 8),
    ("悬疑", 15), ("古言", 16), ("仙侠奇缘", 17), ("现代言情", 18),
    ("浪漫青春", 19), ("玄幻言情", 20), ("悬疑推理", 21), ("科幻空间", 22),
    ("游戏竞技", 23), ("短篇言情", 24), ("同人", 27), ("耽美", 40),
    ("百合", 41), ("轻小说", 42),
]

def with_probe(url):
    return url + "," + json.dumps({"js": probe_js}, ensure_ascii=False)

explore = [
    {"title": "✨ 猫扑小说 · 发现 ✨", "url": with_probe("https://www.mopbook.com/rank.html"),
     "style": {"layout_flexBasisPercent": 1, "layout_wrapBefore": True, "layout_alignSelf": "center"}},
    {"title": "总排行榜", "url": with_probe("https://www.mopbook.com/rank.html"),
     "style": {"layout_flexBasisPercent": 0.32}},
    {"title": "✣ 分类 ✣", "url": with_probe("https://www.mopbook.com/list-1-1/"),
     "style": {"layout_flexBasisPercent": 1, "layout_wrapBefore": True, "layout_alignSelf": "center"}},
]
for name, cid in cats:
    explore.append({"title": name, "url": with_probe("https://www.mopbook.com/list-%d-{{page}}/" % cid),
                    "style": {"layout_flexBasisPercent": 0.32}})

source = {
    "bookSourceName": "猫扑小说",
    "bookSourceType": 0,
    "bookSourceUrl": "https://www.mopbook.com",
    "bookSourceGroup": "小说",
    "bookSourceComment": "书源起点为猫扑小说 mopbook，搜索接口在 rrssk。两站都有人机验证：需要时自动弹出验证页，完成后自动继续，无需手动登录。",
    "bookUrlPattern": r"https?://www\.mopbook\.com/book/[0-9A-Z]+\.html",
    "enabled": True,
    "enabledExplore": True,
    "enabledCookieJar": True,
    "header": json.dumps({"User-Agent": UA, "Accept-Language": "zh-CN,zh;q=0.9"}, ensure_ascii=False),
    "searchUrl": search_js,
    "exploreUrl": json.dumps(explore, ensure_ascii=False),
    "ruleSearch": {
        "bookList": "class.book-card",
        "name": "class.book-card__title@text",
        "author": r"class.book-card__meta-item.0@ownText@js:String(result).replace(/^作者[:：]\s*/,'')",
        "kind": r"class.book-card__meta-item.1@ownText@js:String(result).replace(/^分类[:：]\s*/,'')",
        "intro": "class.book-card__update@text",
        "coverUrl": "class.book-card__cover@img@src",
        "bookUrl": bookurl_js,
    },
    "ruleExplore": {
        "bookList": "class.iBox@li",
        "name": "class.name@a.0@text",
        "author": "class.author@a@text",
        "intro": "class.intro@text",
        "coverUrl": "class.pic@img@src",
        "bookUrl": "class.name@a.0@href",
    },
    "ruleBookInfo": {
        "name": "meta[property=og:novel:book_name]@content",
        "author": "meta[property=og:novel:author]@content",
        "kind": "meta[property=og:novel:category]@content",
        "intro": "meta[property=og:description]@content",
        "coverUrl": "meta[property=og:image]@content",
        "lastChapter": "meta[property=og:novel:latest_chapter_name]@content",
        "tocUrl": toc_js,
    },
    "ruleToc": {
        "chapterList": "$.data[*]",
        "chapterName": "$.chaptername",
        "chapterUrl": "$.chapterurl",
        "nextTocUrl": next_toc_js,
    },
    "ruleContent": {
        "content": "class.content@p@text",
    },
}

out = json.dumps(source, ensure_ascii=False, indent=2)
with io.open("/workspace/猫扑小说.json", "w", encoding="utf-8") as f:
    f.write(out)
print("ok", len(out.encode()))
