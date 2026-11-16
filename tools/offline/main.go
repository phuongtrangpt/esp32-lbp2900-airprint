// Portable local web installer. Standard Go library only; no runtime install.
package main

import (
	"archive/tar"
	"compress/gzip"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"hash/crc32"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

const hpHash = "b9293c793ea8666b6ea9fd624c2bb1f00ef697c3e710cf1189396442df7ad2a2"

func wrap(src []byte) ([]byte, error) {
	if len(src) < 16 || len(src) > 256*1024 || string(src[:2]) != "20" || string(src[8:12]) != "\x7fELF" {
		return nil, fmt.Errorf("invalid HP P1006 source")
	}
	out := append([]byte{}, []byte("\x1b%-12345X@PJL ENTER LANGUAGE=ACL\r\n")...)
	out = append(out, 0, 0xac, 0xc0, 0xde)
	var size [4]byte
	binary.BigEndian.PutUint32(size[:], uint32(len(src)-8))
	out = append(out, size[:]...)
	out = append(out, src...)
	out = append(out, []byte("\x1b%-12345X")...)
	h := sha256.Sum256(out)
	if hex.EncodeToString(h[:]) != hpHash {
		return nil, fmt.Errorf("HP firmware SHA256 mismatch")
	}
	return out, nil
}
func prepareHP(root string) error {
	dir := filepath.Join(root, "firmware", "hp")
	manifestFile := filepath.Join(dir, "web-manifest.json")
	manifestData, e := os.ReadFile(manifestFile)
	if e != nil {
		return e
	}
	var m map[string]interface{}
	if e = json.Unmarshal(manifestData, &m); e != nil {
		return e
	}
	cached := filepath.Join(dir, "sihpP1006.dl")
	dl, _ := os.ReadFile(cached)
	h := sha256.Sum256(dl)
	if hex.EncodeToString(h[:]) != hpHash {
		dl = nil
		fmt.Println("HP P1006: downloading upstream firmware once; later runs work offline...")
		client := http.Client{Timeout: 40 * time.Second}
		r, e := client.Get("https://www.quirinux.org/printers/sihpP1006.tar.gz")
		if e != nil {
			return e
		}
		defer r.Body.Close()
		if r.StatusCode != 200 {
			return fmt.Errorf("upstream HTTP %d", r.StatusCode)
		}
		gz, e := gzip.NewReader(io.LimitReader(r.Body, 300*1024+1))
		if e != nil {
			return e
		}
		defer gz.Close()
		tr := tar.NewReader(io.LimitReader(gz, 512*1024+1))
		for {
			hdr, err := tr.Next()
			if err == io.EOF {
				break
			}
			if err != nil {
				return err
			}
			if hdr.Name == "sihpP1006.img" {
				if hdr.Size > 256*1024 {
					return fmt.Errorf("oversized HP firmware")
				}
				src, err := io.ReadAll(io.LimitReader(tr, 256*1024+1))
				if err != nil {
					return err
				}
				dl, e = wrap(src)
				if e != nil {
					return e
				}
				break
			}
		}
		if len(dl) < 32 {
			return fmt.Errorf("HP firmware not found")
		}
		if e = os.WriteFile(cached, dl, 0600); e != nil {
			return e
		}
	}
	part := make([]byte, 12+len(dl))
	binary.LittleEndian.PutUint32(part, 0x36504846)
	binary.LittleEndian.PutUint32(part[4:], uint32(len(dl)))
	binary.LittleEndian.PutUint32(part[8:], crc32.ChecksumIEEE(dl))
	copy(part[12:], dl)
	if e = os.WriteFile(filepath.Join(dir, "hp-firmware.bin"), part, 0600); e != nil {
		return e
	}
	digest := sha256.Sum256(part)
	builds, ok := m["builds"].([]interface{})
	if !ok || len(builds) != 1 {
		return fmt.Errorf("invalid manifest")
	}
	build := builds[0].(map[string]interface{})
	parts := build["parts"].([]interface{})
	kept := make([]interface{}, 0, len(parts)+1)
	for _, p := range parts {
		if p.(map[string]interface{})["path"] != "hp-firmware.bin" {
			kept = append(kept, p)
		}
	}
	build["parts"] = append(kept, map[string]interface{}{"path": "hp-firmware.bin", "offset": 0x840000, "sha256": hex.EncodeToString(digest[:])})
	updated, e := json.MarshalIndent(m, "", "  ")
	if e != nil {
		return e
	}
	if e = os.WriteFile(manifestFile, updated, 0600); e != nil {
		return e
	}
	fmt.Println("HP P1006: verified firmware cached; it will be flashed automatically.")
	return nil
}
func main() {
	exe, _ := os.Executable()
	base := filepath.Dir(exe)
	rootFlag := flag.String("root", base, "installer folder")
	prepare := flag.Bool("prepare-only", false, "prepare HP cache and exit")
	skip := flag.Bool("skip-hp", false, "skip HP preparation")
	noBrowser := flag.Bool("no-browser", false, "do not open browser")
	flag.Parse()
	root, _ := filepath.Abs(*rootFlag)
	if !*skip {
		if e := prepareHP(root); e != nil {
			fmt.Println("HP automatic preparation:", e)
			fmt.Println("Canon and already-configured ESP updates still work. First HP setup needs Internet once.")
			if *prepare {
				os.Exit(1)
			}
		}
	}
	if *prepare {
		return
	}
	if _, e := os.Stat(filepath.Join(root, "index.html")); e != nil {
		fmt.Println("Keep this program next to index.html and firmware/. Extract the whole ZIP first.")
		os.Exit(1)
	}
	listener, e := net.Listen("tcp", "127.0.0.1:8787")
	if e != nil {
		listener, e = net.Listen("tcp", "127.0.0.1:0")
	}
	if e != nil {
		fmt.Println(e)
		os.Exit(1)
	}
	url := "http://" + listener.Addr().String() + "/"
	fmt.Println("ESP 32 offline installer:", url)
	fmt.Println("Use Chrome/Edge. Keep this window open while flashing. Ctrl+C to stop.")
	if !*noBrowser && runtime.GOOS == "windows" {
		exec.Command("rundll32", "url.dll,FileProtocolHandler", url).Start()
	}
	fs := http.FileServer(http.Dir(root))
	server := http.Server{ReadHeaderTimeout: 5 * time.Second, Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "GET" && r.Method != "HEAD" {
			w.WriteHeader(405)
			return
		}
		if strings.Contains(r.URL.Path, "..") {
			w.WriteHeader(400)
			return
		}
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		fs.ServeHTTP(w, r)
	})}
	if e = server.Serve(listener); e != nil {
		fmt.Println(e)
	}
}

// The cached HP firmware is private local user data; it is never uploaded here.
