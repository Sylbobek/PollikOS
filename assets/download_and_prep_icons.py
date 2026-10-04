import urllib.request
import io
import os
from PIL import Image, ImageDraw, ImageFilter
import numpy as np

ASSETS_DIR = os.path.abspath(os.path.dirname(__file__))

# Sources from high-res open source / macOS extracted repositories
URLS = {
    'AboutSystemIcon.png': 'https://raw.githubusercontent.com/zagnut531/macOS-26-Icons/main/Utilities/System%20Information.png',
    'FilesIcon.png': 'https://raw.githubusercontent.com/zagnut531/macOS-26-Icons/main/Finder.png',
    'TerminalIcon.png': 'https://raw.githubusercontent.com/zagnut531/macOS-26-Icons/main/Utilities/Terminal.png',
    'NotepadIcon.png': 'https://raw.githubusercontent.com/zagnut531/macOS-26-Icons/main/Notes.png',
    'SettingsIcon.png': 'https://raw.githubusercontent.com/zagnut531/macOS-26-Icons/main/System%20Settings.png',
    'BrowserIcon.png': 'https://raw.githubusercontent.com/zagnut531/macOS-26-Icons/main/Safari.png',
    'FolderIcon.png': 'https://raw.githubusercontent.com/deathrashed/iconography/main/macos/folders/colors/png/z-yellow.png',
    'FolderBlueIcon.png': 'https://raw.githubusercontent.com/deathrashed/iconography/main/macos/folders/colors/png/z-blue.png',
}

def remove_white_bg(img, threshold=245):
    """Make white or near-white background pixels transparent with smooth alpha roll-off."""
    img = img.convert("RGBA")
    arr = np.array(img, dtype=np.float32)
    r, g, b, a = arr[:,:,0], arr[:,:,1], arr[:,:,2], arr[:,:,3]
    # Brightness / distance from white
    dist = np.maximum(255 - r, np.maximum(255 - g, 255 - b))
    # Soft alpha mask
    new_a = np.clip(dist * 255.0 / (255.0 - threshold), 0, 255)
    # If already had alpha, keep min
    arr[:,:,3] = np.minimum(a, new_a)
    return Image.fromarray(arr.astype(np.uint8), "RGBA")

def fetch_and_save():
    print("Fetching high-resolution icons...")
    for name, url in URLS.items():
        dst = os.path.join(ASSETS_DIR, name)
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=10) as resp:
                data = resp.read()
                im = Image.open(io.BytesIO(data)).convert("RGBA")
                # Resize to standard 512x512
                im = im.resize((512, 512), Image.Resampling.LANCZOS)
                im.save(dst, "PNG")
                print(f"Saved {name} ({im.size})")
        except Exception as e:
            print(f"Failed to fetch {name}: {e}")

    # Process generated images for Trash, PollikMark, and TextFile
    artifact_dir = r"C:\Users\syltu\.gemini\antigravity-ide\brain\241dc7fa-14e2-4fdf-8ed7-423c550d4ad8"
    gen_map = {
        'TrashIcon.png': os.path.join(artifact_dir, 'macos_trash_icon_1789801327680.jpg'),
        'PollikmarkIcon.png': os.path.join(artifact_dir, 'pollikmark_3d_icon_1789801344229.jpg'),
        'TextFileIcon.png': os.path.join(artifact_dir, 'macos_text_file_icon_1789801358485.jpg'),
    }

    for name, src_path in gen_map.items():
        dst = os.path.join(ASSETS_DIR, name)
        if os.path.exists(src_path):
            im = Image.open(src_path).convert("RGB")
            # For PollikMark squircle icon, apply squircle mask
            if name == 'PollikmarkIcon.png':
                im_rgba = im.convert("RGBA").resize((512, 512), Image.Resampling.LANCZOS)
                mask = Image.new("L", (512, 512), 0)
                draw = ImageDraw.Draw(mask)
                draw.rounded_rectangle((10, 10, 502, 502), radius=110, fill=255)
                im_rgba.putalpha(mask)
                im_rgba.save(dst, "PNG")
                print(f"Processed and saved {name}")
            else:
                # Remove white background
                im_rgba = remove_white_bg(im.resize((512, 512), Image.Resampling.LANCZOS))
                im_rgba.save(dst, "PNG")
                print(f"Processed and saved {name}")
        else:
            print(f"Source not found for {name}: {src_path}")

if __name__ == '__main__':
    fetch_and_save()
