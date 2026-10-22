#pragma once
#include <QString>
#include <QRegularExpression>

namespace WizReaderOpeningPresentation {
inline void removeBootstrap(QString& html)
{
    // Cached temporary HTML can be reopened directly in edit mode. Remove
    // only our exact unsaved bootstrap before deciding whether to add it again.
    const QRegularExpression ownScript(
        QStringLiteral("<script\\s+id=[\"']wiz_reader_opening_bootstrap[\"']\\s+name=[\"']wiz_inner_script[\"']\\s+wiz_style=[\"']unsave[\"']\\s*>[\\s\\S]*?</script\\s*>"),
        QRegularExpression::CaseInsensitiveOption);
    html.remove(ownScript);
}
// Insert in the temporary reader HTML head, never in saved note source. This
// is presentation only, not an access/lock gate; native authorization is intact.
inline QString bootstrapHtml()
{
    return QString::fromLatin1(R"HTML(<script id="wiz_reader_opening_bootstrap" name="wiz_inner_script" wiz_style="unsave">(function(){
if(document.getElementById('wiz_tmp_style_reader_opening'))return;
// Install only when this bootstrap can execute. An inline-script CSP refusal
// must not leave an unconditional hidden stylesheet with no fallback timer.
var style=document.createElement('style');style.id='wiz_tmp_style_reader_opening';
style.setAttribute('name','wiz_inner_style');style.setAttribute('wiz_style','unsave');
style.textContent='@media screen{html{opacity:0!important}}';document.head.appendChild(style);
var finished=false,frames=[],observed=null;
function show(){
  if(finished)return;finished=true;clearInterval(poll);clearTimeout(deadline);
  frames.forEach(cancelAnimationFrame);window.removeEventListener('pagehide',show);
  if(style.isConnected)style.remove();
}
function check(){
  if(finished)return;
  if(document.body&&document.body.isContentEditable){show();return;}
  var state=window.WizNativeEditorSetup;
  if(!state)return;
  if(state.state==='failed'){show();return;}
  if(state.state!=='ready'||observed===state)return;
  observed=state;
  frames.push(requestAnimationFrame(function(){frames.push(requestAnimationFrame(function(){
    if(finished)return;
    // A late completion cannot reveal a superseded setup. Retry observing the
    // new token instead; no reload, DOM clearing or focus change is performed.
    if(window.WizNativeEditorSetup!==state||state.state!=='ready'){observed=null;return;}
    show();
  }));}));
}
var poll=setInterval(check,25),deadline=setTimeout(show,30000);
window.addEventListener('pagehide',show,{once:true});check();
})();</script>)HTML");
}
}
