"""AnthologyBuilder's build for the Android app (run through Chaquopy):
headless() into the app's files dir, without the Data Select previews (they
are recorded by playing the PC game). Its log is builder.log there."""
from pathlib import Path


def build(game_dir, args):
    import trilogy_builder as tb
    tb.GAME_DIR = Path(str(game_dir))
    return tb.headless([str(a) for a in args] + ['--no-previews'])
