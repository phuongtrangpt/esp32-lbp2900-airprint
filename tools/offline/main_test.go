package main

import (
	"encoding/binary"
	"encoding/json"
	"hash/crc32"
	"os"
	"path/filepath"
	"testing"
)

func TestWrapRejects(t *testing.T) {
	for _, b := range [][]byte{nil, []byte("firmware.bin"), make([]byte, 32)} {
		if _, e := wrap(b); e == nil {
			t.Fatal("invalid firmware accepted")
		}
	}
}
func TestPrivateCachedPreparation(t *testing.T) {
	path := os.Getenv("HP_SOURCE_IMAGE")
	if path == "" {
		t.Skip("private upstream fixture not supplied")
	}
	src, e := os.ReadFile(path)
	if e != nil {
		t.Fatal(e)
	}
	dl, e := wrap(src)
	if e != nil {
		t.Fatal(e)
	}
	root := t.TempDir()
	dir := filepath.Join(root, "firmware", "hp")
	os.MkdirAll(dir, 0700)
	os.WriteFile(filepath.Join(dir, "sihpP1006.dl"), dl, 0600)
	os.WriteFile(filepath.Join(dir, "web-manifest.json"), []byte(`{"name":"HP LaserJet P1006 ESP 32","builds":[{"parts":[{"path":"firmware.bin","offset":131072}]}]}`), 0600)
	for i := 0; i < 2; i++ {
		if e = prepareHP(root); e != nil {
			t.Fatal(e)
		}
	}
	b, _ := os.ReadFile(filepath.Join(dir, "hp-firmware.bin"))
	if binary.LittleEndian.Uint32(b) != 0x36504846 || int(binary.LittleEndian.Uint32(b[4:])) != len(dl) || binary.LittleEndian.Uint32(b[8:]) != crc32.ChecksumIEEE(dl) {
		t.Fatal("invalid partition image")
	}
	b, _ = os.ReadFile(filepath.Join(dir, "web-manifest.json"))
	var m map[string]interface{}
	json.Unmarshal(b, &m)
	parts := m["builds"].([]interface{})[0].(map[string]interface{})["parts"].([]interface{})
	if len(parts) != 2 {
		t.Fatal("manifest duplicated parts")
	}
	if parts[1].(map[string]interface{})["offset"] != float64(0x840000) {
		t.Fatal("wrong HP offset")
	}
}
