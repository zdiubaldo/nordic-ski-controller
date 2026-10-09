const {defineConfig}=require('@playwright/test');
const baseURL=process.env.NORDIC_BASE_URL;
if(!baseURL)throw new Error('Set NORDIC_BASE_URL to the ESP32 URL (normally http://192.168.4.1).');
const url=new URL(baseURL);
if(url.protocol!=='http:'||url.username||url.password)throw new Error('Use a plain HTTP device URL without credentials.');
module.exports=defineConfig({
  testDir:'./tests/device',fullyParallel:false,workers:1,retries:0,forbidOnly:true,
  timeout:30000,expect:{timeout:10000},
  outputDir:'.local/device-results',
  reporter:[['list'],['html',{outputFolder:process.env.NORDIC_REPORT_DIR||'.local/device-report',open:'never'}]],
  use:{baseURL,channel:'chrome',headless:true,viewport:{width:1180,height:820},
    actionTimeout:10000,navigationTimeout:15000,trace:'retain-on-failure',screenshot:'only-on-failure'},
});
