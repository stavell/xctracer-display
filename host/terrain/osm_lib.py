"""Helpers shared by the OSM size study and the map render."""
TR = {"а": "a", "б": "b", "в": "v", "г": "g", "д": "d", "е": "e", "ж": "zh", "з": "z", "и": "i", "й": "y", "к": "k", "л": "l",
      "м": "m", "н": "n", "о": "o", "п": "p", "р": "r", "с": "s", "т": "t", "у": "u", "ф": "f", "х": "h", "ц": "ts", "ч": "ch",
      "ш": "sh", "щ": "sht", "ъ": "a", "ь": "y", "ю": "yu", "я": "ya"}


def translit(s):
    o = []
    for ch in s:
        l = ch.lower()
        if l in TR:
            t = TR[l]
            o.append(t.capitalize() if ch != l else t)
        else:
            o.append(ch)
    return "".join(o)


def merge_ways(ways):
    """Join ways of the same class that meet end to end at a node used by exactly two ways (OSM splits roads
    at every junction/attribute change; chains simplify much better)."""
    from collections import defaultdict
    ends = defaultdict(list)
    for i, w in enumerate(ways):
        ends[w["pts"][0]].append((i, 0)); ends[w["pts"][-1]].append((i, 1))
    used = set(); out = []
    for i, w in enumerate(ways):
        if i in used:
            continue
        used.add(i)
        chain = list(w["pts"])
        for side in (1, 0):
            while True:
                node = chain[-1] if side else chain[0]
                inc = ends[node]
                cand = [(j, e) for (j, e) in inc if j not in used and ways[j]["cls"] == w["cls"]]
                if len(inc) != 2 or not cand:
                    break
                j, e = cand[0]; used.add(j)
                pj = ways[j]["pts"]
                if side:
                    chain += (pj[1:] if e == 0 else pj[::-1][1:])
                else:
                    chain = (pj[:-1] if e == 1 else pj[::-1][:-1]) + chain
        out.append(dict(cls=w["cls"], pts=chain))
    return out


