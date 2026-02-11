import os
import subprocess
import sys
import time

def run_ssg():
    start = time.time()
    res = subprocess.run(["ssg.exe", "content_loadtest"], capture_output=True, text=True)
    duration = (time.time() - start) * 1000
    out = res.stdout
    # Parse built, skipped, failed
    built = 0
    skipped = 0
    failed = 0
    for line in out.splitlines():
        if "BUILD COMPLETE" in line:
            parts = line.split(" - ")[1].split(", ")
            built = int(parts[0].split()[0])
            skipped = int(parts[1].split()[0])
            failed = int(parts[2].split()[0])
    return built, skipped, failed, duration, out

def run_test():
    print("==================================================")
    print(" RUNNING INCREMENTAL BUILD TEST SUITE")
    print("==================================================")

    # Test 1: Warm skip
    b, s, f, dur, _ = run_ssg()
    print(f"\n[Test 1] Baseline Skip (No changes):")
    print(f"         Built: {b}, Skipped: {s}, Failed: {f} | Time: {dur:.1f} ms")
    assert b == 0 and s >= 10000, f"Expected 0 built, got {b}"
    print("         ==> PASSED (100% cache hit)")

    # Test 2: Modify 1 file's content
    target_md = os.path.join("content_loadtest", "blog", "vol0", "post1.md")
    canary = f"CANARY_TEST_VALUE_{int(time.time())}"
    with open(target_md, "a") as fp:
        fp.write(f"\n\n{canary}\n")

    b, s, f, dur, out = run_ssg()
    print(f"\n[Test 2] Edit Single File (post1.md):")
    print(f"         Built: {b}, Skipped: {s}, Failed: {f} | Time: {dur:.1f} ms")
    assert b == 1, f"Expected 1 built, got {b}"
    # Verify the output file contains the canary
    target_html = os.path.join("dist", "blog", "vol0", "post1.html")
    with open(target_html, "r") as fp:
        html_content = fp.read()
    # Normalize underscores for check if markdown italicized it or check directly
    clean_canary = canary.replace("_", "")
    clean_html = html_content.replace("<em>", "").replace("</em>", "").replace("_", "")
    assert clean_canary in clean_html, "Output HTML did not contain updated content!"
    print("         ==> PASSED (Only post1.md re-rendered and verified)")

    # Test 3: Touch mtime without changing content (simulate git checkout)
    print(f"\n[Test 3] Simulating 'git checkout' (mtime changed, content identical):")
    target_md2 = os.path.join("content_loadtest", "blog", "vol0", "post2.md")
    now = time.time() + 10
    os.utime(target_md2, (now, now))
    b, s, f, dur, _ = run_ssg()
    print(f"         Built: {b}, Skipped: {s}, Failed: {f} | Time: {dur:.1f} ms")
    assert b == 0, f"Expected 0 built on mtime-only touch, got {b}"
    print("         ==> PASSED (SHA-256 fallback prevented unnecessary render)")

    # Test 4: Add brand new post
    ts = int(time.time())
    new_md = os.path.join("content_loadtest", "blog", "vol0", f"test_new_post_{ts}.md")
    new_html = os.path.join("dist", "blog", "vol0", f"test_new_post_{ts}.html")
    with open(new_md, "w") as fp:
        fp.write("# Brand New Post\n\nTesting new post creation.")

    b, s, f, dur, _ = run_ssg()
    print(f"\n[Test 4] Add Brand New File (test_new_post_{ts}.md):")
    print(f"         Built: {b}, Skipped: {s}, Failed: {f} | Time: {dur:.1f} ms")
    assert b == 1, f"Expected 1 built, got {b}"
    assert os.path.exists(new_html), "Output HTML was not generated!"
    print("         ==> PASSED (New file detected and built)")
    # Cleanup
    if os.path.exists(new_md): os.remove(new_md)
    if os.path.exists(new_html): os.remove(new_html)

    # Test 5: Template change (global invalidation)
    tpl_path = os.path.join("templates", "layout.html")
    with open(tpl_path, "r") as fp:
        tpl_orig = fp.read()

    try:
        with open(tpl_path, "w") as fp:
            fp.write(tpl_orig + "\n<!-- template_test_marker -->\n")

        print(f"\n[Test 5] Modify Template (layout.html):")
        b, s, f, dur, _ = run_ssg()
        print(f"         Built: {b}, Skipped: {s}, Failed: {f} | Time: {dur:.1f} ms")
        assert b >= 10000, f"Expected all files to rebuild, but only {b} built!"
        print("         ==> PASSED (Global template invalidation correctly rebuilt all pages)")
    finally:
        with open(tpl_path, "w") as fp:
            fp.write(tpl_orig)
        # Restore cache state with original template
        print("         Restoring baseline with original template...")
        run_ssg()

    print("\n==================================================")
    print(" ALL 5 TESTS PASSED SUCCESSFULLY! ")
    print("==================================================")

if __name__ == "__main__":
    run_test()
