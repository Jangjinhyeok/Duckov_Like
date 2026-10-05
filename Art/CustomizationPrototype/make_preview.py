"""같은 .blend에서 렌더한 여섯 장을 비교용 contact sheet로 배치한다."""

from pathlib import Path

from PIL import Image, ImageDraw


art = Path(__file__).resolve().parent
evidence = art.parents[1] / "Saved/Automation/CustomizationAssets"
board = Image.new("RGB", (1080, 796), (47, 57, 69))
draw = ImageDraw.Draw(board)
for row, part in enumerate(("A", "B")):
    for col, weight in enumerate((0.0, 0.5, 1.0)):
        path = evidence / f"SK_CustomizationPart{part}_Shape{weight}.png"
        with Image.open(path) as rendered:
            assert rendered.size == (720, 720)
            tile = rendered.convert("RGB").resize((360, 360), Image.Resampling.LANCZOS)
        board.paste(tile, (col * 360, row * 398))
        label = f"Part {part} | Shape {2 * weight - 1:+.1f} | weight {weight}"
        draw.text((col * 360 + 20, row * 398 + 370), label, fill=(239, 239, 239), font_size=16)
board.save(art / "Preview.png")
print("C3 preview: PASS")
