import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
export default defineConfig({
  plugins:[{
    name:'recoverable-esp-flash',
    enforce:'pre',
    transform(code,id){
      if(id.replaceAll('\\','/').endsWith('/esp-web-tools/dist/install-dialog.js')){
        const marker='        ${content}';
        if(!code.includes(marker))throw new Error('ESP dialog layout changed: progress console injection needs review');
        return code.replace(marker,marker+'<vp-flash-progress slot="content" hidden></vp-flash-progress>');
      }
    },
    resolveId(source,importer){
      if(source==='./flash' && importer?.replaceAll('\\','/').endsWith('/esp-web-tools/dist/install-dialog.js'))
        return fileURLToPath(new URL('./flash.js',import.meta.url));
    },
  }],
  build:{license:true},
});
