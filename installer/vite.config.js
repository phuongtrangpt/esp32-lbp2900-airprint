import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
export default defineConfig({
  plugins:[{
    name:'recoverable-esp-flash',
    enforce:'pre',
    resolveId(source,importer){
      if(source==='./flash' && importer?.replaceAll('\\','/').endsWith('/esp-web-tools/dist/install-dialog.js'))
        return fileURLToPath(new URL('./flash.js',import.meta.url));
    },
  }],
  build:{license:true},
});
