import test from 'node:test';
import assert from 'node:assert/strict';
import { wifiSignal } from '../src/lib/wifi-signal.ts';
test('Wi-Fi bars and captions agree at every quality boundary', () => {
  for (const [rssi,bars,label] of [[-30,4,'Wi-Fi tốt'],[-60,4,'Wi-Fi tốt'],[-61,3,'Wi-Fi khá'],[-70,3,'Wi-Fi khá'],[-71,2,'Wi-Fi yếu'],[-80,2,'Wi-Fi yếu'],[-81,1,'Wi-Fi rất yếu'],[-100,1,'Wi-Fi rất yếu']] as const) {
    const state=wifiSignal(true,rssi);
    assert.equal(state.bars,bars);assert.equal(state.label,label);assert.equal(state.weak,bars<=2);assert.equal(state.rssi,rssi);
  }
});
test('Unknown readings and HTTP errors never look like good or weak signal',()=>{
  for(const value of [0,null,undefined,'-70',NaN,Infinity,12,-128,-70.5])assert.equal(wifiSignal(true,value).rssi,null);
  assert.equal(wifiSignal(null,-45).bars,0);
  assert.equal(wifiSignal(false,-45).label,'Wi-Fi chưa kết nối');
});
