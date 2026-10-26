"""Write a synthetic Today bundle (HINKDAILY1) for daily-probe: python make-wire.py OUT [YYYY-MM-DD]"""
import sys
import uuid
from datetime import date

NS = uuid.UUID("5d1c1e3a-0d6f-5c21-8a7b-2f9e4b6a1c33")
day = sys.argv[2] if len(sys.argv) > 2 else date.today().isoformat()
doc = str(uuid.uuid5(NS, "document" + day))


def entry(kind, key, title, when, context="-"):
    return "\t".join(["ENTRY", kind, str(uuid.uuid5(NS, doc + key)), title, when, context])


lines = ["ready", "FORMAT\tHINKDAILY1", "DOCUMENT\t" + doc, "DATE\t" + day, "ZONE\tUTC",
         entry("meeting", "garden", "Garden plans with Alex", "08:00 - 08:45", "Bring the balcony measurements."),
         entry("meeting", "club", "Book club", "16:30 - 17:30"),
         entry("task", "bike", "Book the bicycle repair", "Due " + day),
         entry("task", "seeds", "Pick up seedlings", "Due " + day),
         entry("capture", "today", "Open notebook", "Capture")]
open(sys.argv[1], "w", newline="\n").write("\n".join(lines) + "\n")
