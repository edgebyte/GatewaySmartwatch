#pragma once
#include <Arduino.h>

static const char CONFIG_PAGE[] PROGMEM = R"GWHTML(<!doctype html>
<html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Edgebyte Gateway • ตั้งค่าอุปกรณ์</title>
<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#0d1422;color:#edf3ff}
*{box-sizing:border-box}body{margin:0}main{max-width:960px;margin:auto;padding:32px 20px 60px}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:28px;gap:16px}
.brand{font-size:13px;letter-spacing:2px;color:#62d7d0;font-weight:700}h1{font-size:28px;margin:7px 0}p{line-height:1.6;color:#a8b8d0}
.badge{border:1px solid #2b405a;border-radius:99px;padding:7px 13px;font-size:12px;white-space:nowrap}
.grid{display:grid;grid-template-columns:1.25fr 1fr;gap:20px}.card{background:#152033;border:1px solid #283850;padding:24px;border-radius:18px}
h2{font-size:18px;margin:0 0 18px}label{display:block;font-size:14px;margin:18px 0 7px}
input{width:100%;background:#0e1727;border:1px solid #364761;color:#f0f5ff;border-radius:9px;padding:12px;font:inherit}
input:focus{outline:2px solid #62d7d0;outline-offset:1px}small{display:block;color:#97aac6;line-height:1.6;margin-top:7px}
button{background:#62d7d0;color:#092426;padding:12px 18px;border:0;border-radius:9px;font:inherit;font-weight:700;cursor:pointer;margin-top:22px}
button:disabled{opacity:.5;cursor:wait}.check{display:flex;align-items:center;gap:9px;margin:10px 0}.check input{width:auto}
dl{margin:0}dt{font-size:12px;color:#97aac6;margin-top:17px}dd{margin:5px 0;overflow-wrap:anywhere;font-size:15px}
hr{border:0;border-top:1px solid #2a3a51;margin:24px 0}.notice{padding:13px;background:#102d34;border-radius:10px;color:#a1eee4}
#message{white-space:pre-wrap;line-height:1.6;min-height:24px;margin-top:16px}.error{color:#ff9c9c}.ok{color:#83e3b7}
.foot{margin-top:24px;font-size:12px;color:#8d9db8}@media(max-width:680px){.grid{grid-template-columns:1fr}header{align-items:flex-start}h1{font-size:23px}.card{padding:20px}}
</style></head><body><main>
<header><div><div class="brand">EDGEBYTE / SMARTWATCH GATEWAY</div><h1>ตั้งค่า gateway</h1><p>บันทึก MAC Address และเครือข่ายไว้ในอุปกรณ์</p></div><span class="badge">Firmware 2.1</span></header>
<div class="grid"><section class="card"><h2>อุปกรณ์ที่อนุญาต</h2>
<form id="config"><label for="mac">Smartwatch MAC Address</label><input id="mac" name="mac" maxlength="17" placeholder="69:43:2D:20:8E:87" autocomplete="off" spellcheck="false">
<small>รองรับ 1 เรือน • เว้นว่างเพื่อหยุดรับข้อมูลนาฬิกาทั้งหมด</small>
<p class="notice">รับและถอดข้อมูลเฉพาะ MAC ที่บันทึกไว้ โดยอ่าน BLE Broadcast ไม่ต้องจับคู่กับนาฬิกา</p>
<hr><h2>Wi-Fi ของหน้างาน (ไม่บังคับ)</h2>
<label for="ssid">ชื่อเครือข่าย / SSID</label><input id="ssid" name="ssid" maxlength="32" autocomplete="off">
<label for="wifi_password">รหัสผ่าน Wi-Fi ใหม่</label><input id="wifi_password" name="wifi_password" type="password" maxlength="64" autocomplete="new-password">
<small>เว้นว่างเพื่อเก็บรหัสเดิม หากเปลี่ยนไปเครือข่ายใหม่ ให้กรอกรหัสใหม่ด้วย</small>
<label class="check"><input type="checkbox" id="clear_wifi_password" name="clear_wifi_password" value="1">ใช้ Wi-Fi แบบไม่มีรหัสผ่าน</label>
<small>ล้างชื่อ SSID เพื่อยกเลิกการเชื่อมต่อ Wi-Fi หน้างาน โดยยังเข้าผ่าน Wi-Fi ของ gateway ได้</small>
<hr><h2>รหัสเข้าใช้งาน gateway</h2><label for="access_password">รหัสใหม่ (เว้นว่างเพื่อใช้เดิม)</label>
<input id="access_password" name="access_password" type="password" minlength="8" maxlength="32" autocomplete="new-password">
<small>8–32 ตัวอักษรภาษาอังกฤษ/ตัวเลข/สัญลักษณ์ ไม่เว้นวรรค ใช้ทั้งล็อกอิน admin และ Wi-Fi ของ gateway</small>
<button id="save" type="submit">บันทึกการตั้งค่า</button><div id="message" role="status"></div></form></section>
<aside class="card"><h2>สถานะปัจจุบัน</h2><dl>
<dt>MAC ที่บันทึก</dt><dd id="target">กำลังโหลด…</dd><dt>การสแกน BLE</dt><dd id="ble">—</dd>
<dt>นาฬิกาที่รับล่าสุด</dt><dd id="watch">—</dd>
<dt>Wi-Fi สำหรับตั้งค่า</dt><dd id="ap">—</dd><dt>IP Wi-Fi ของ gateway</dt><dd id="ap_ip">—</dd>
<dt>Wi-Fi หน้างาน</dt><dd id="wifi">—</dd><dt>IP Wi-Fi หน้างาน</dt><dd id="wifi_ip">—</dd>
<dt>LAN / W5500</dt><dd id="lan">—</dd><dt>IP LAN (DHCP)</dt><dd id="lan_ip">—</dd>
<dt>แพ็กเก็ตตกหล่นในคิว</dt><dd id="dropped">—</dd></dl>
<p class="foot">เข้าหน้านี้ผ่าน IP ของช่องทางใดก็ได้ บันทึก MAC แล้วมีผลทันที การเปลี่ยน Wi-Fi หรือรหัสเข้าใช้งานจะรีสตาร์ตอุปกรณ์</p>
<p class="foot">ใช้ MAC ที่แสดงจริงจาก BLE scanner หากลำดับไบต์ในเอกสารผู้ผลิตไม่ตรงกัน</p></aside></div>
<p class="foot">Local configuration • ไม่มีการส่งการตั้งค่าไปบริการภายนอก</p></main>
<script>
const csrf='%CSRF%';let initialized=false;let timer;
const byId=id=>document.getElementById(id);
const text=(id,value)=>{byId(id).textContent=value};
async function refresh(){try{
 const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error('ไม่สามารถอ่านสถานะได้ ('+r.status+')');
 const s=await r.json();
 if(!initialized){byId('mac').value=s.target_mac;byId('ssid').value=s.wifi_ssid;initialized=true;byId('save').disabled=false}
 text('target',s.target_mac||'ยังไม่ได้กำหนด — หยุดรับข้อมูล');text('ble',s.ble.scanning?'กำลังสแกน':'กำลังเริ่มรอบสแกน');
 text('watch',s.ble.last_mac?`${s.ble.name||s.ble.last_mac} • RSSI ${s.ble.rssi_dbm} dBm • รับล่าสุด ${Math.floor(s.ble.age_ms/1000)} วินาทีก่อน`:'ยังไม่พบ MAC ที่กำหนด');
 text('ap',s.ap_ssid);text('ap_ip',s.ap_ip);text('wifi',s.wifi_connected?s.wifi_ssid+' • เชื่อมต่อแล้ว':(s.wifi_ssid?'ยังไม่เชื่อมต่อ':'ไม่ได้กำหนด'));
 text('wifi_ip',s.wifi_ip==='0.0.0.0'?'—':s.wifi_ip);text('lan',!s.lan_enabled?'ปิดใน Config.h':(!s.lan_started?'เริ่ม W5500 ไม่สำเร็จ':(s.lan_link?'มีสายเชื่อมต่อ':'รอการเชื่อมต่อสาย')));
 text('lan_ip',s.lan_ip==='0.0.0.0'?'รอ DHCP':s.lan_ip);text('dropped',s.ble.queue_dropped);
 }catch(e){if(!initialized){text('message',e.message);byId('message').className='error'}}}
byId('save').disabled=true;
byId('config').addEventListener('submit',async event=>{event.preventDefault();byId('save').disabled=true;text('message','กำลังบันทึก…');byId('message').className='';
 const data=new URLSearchParams(new FormData(event.target));data.set('csrf',csrf);
 try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:data});
 const s=await r.json();if(!r.ok)throw new Error(s.error||'บันทึกไม่สำเร็จ');
 byId('mac').value=s.target_mac;byId('wifi_password').value='';byId('access_password').value='';byId('clear_wifi_password').checked=false;
 byId('message').className='ok';text('message',s.restarting?'บันทึกแล้ว กำลังรีสตาร์ต… เชื่อมต่อเครือข่ายแล้วเปิดหน้านี้ใหม่ หากเปลี่ยนรหัสให้ล็อกอินด้วยรหัสใหม่':'บันทึกแล้ว MAC ใหม่มีผลทันที');
 if(s.restarting){clearInterval(timer);return}await refresh();
 }catch(e){byId('message').className='error';text('message',e.message)}byId('save').disabled=false;});
refresh();timer=setInterval(refresh,3000);
</script></body></html>)GWHTML";
