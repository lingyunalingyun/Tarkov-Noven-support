import copy
import unittest
import json
from generate import normalize, build, escape, unique_object, craft_quantity

def fixture():
    level = dict(id="s-1", level=1, constructionTime=60,
                 itemRequirements=[dict(item="item", count=3)],
                 stationLevelRequirements=[], skillRequirements=[dict(skill="Attention", level=2)],
                 traderRequirements=[dict(trader="t", value=2, requirementType="level", compareMethod=">=")])
    return {"data": {"s": dict(id="s", name="station-key", levels=[level])}}

class GeneratorTests(unittest.TestCase):
    def test_craft_fraction(self):
        self.assertEqual(craft_quantity(.66),"0.66")
        for value in (0,-1,True,float("inf"),float("nan"),"2"):
            with self.assertRaises(ValueError): craft_quantity(value)

    def test_crafts_images(self):
        source=fixture();source["data"]["s"]["imageLink"]="https://assets.tarkov.dev/station-workbench.png"
        craft=dict(id="c",station="s",level=1,duration=100,productItem=dict(item="out",count=2),
            requiredItems=[dict(item="in",count=.66,attributes=dict(tool=True))],taskUnlock="task")
        locales={"en":{},"zh":{}}
        rows=normalize(source,locales,{"t":{"name":"trader"}},[craft])
        self.assertEqual(rows["station_images"],[("s","station-workbench")])
        self.assertEqual(rows["craft_materials"],[("c","in","0.66",1,0)])
        self.assertIn("taskUnlock",rows["crafts"][0][-1])
        with self.assertRaises(ValueError): normalize(source,locales,{"t":{"name":"trader"}},[craft,craft])
        source["data"]["s"]["imageLink"]="https://example.com/station-workbench.png"
        with self.assertRaises(ValueError): normalize(source,locales,{"t":{"name":"trader"}},[craft])

    def test_duplicate_json(self):
        with self.assertRaises(ValueError):
            json.loads('{"s":{},"s":{}}',object_pairs_hook=unique_object)

    def normalized(self, source=None):
        return normalize(source or fixture(), {"zh":{"station-key":"工作台","Attention":"注意力","trader":"商人"},
            "en":{"station-key":"Workbench","Attention":"Attention","trader":"Trader"}}, {"t":{"name":"trader"}})

    def test_fixture_localized_identity(self):
        rows=self.normalized()
        self.assertEqual(rows["stations"],[("s","工作台","Workbench")])
        self.assertEqual(rows["skill_requirements"][0][1],"Attention")
        output,meta=build(rows,rows,{"item"})
        self.assertEqual(meta["missingItemIds"],[])
        self.assertIn("regular\ts-1\titem\t3\n",output["hideout_item_requirements.tsv"])

    def test_deterministic(self):
        a=fixture(); a["data"]["z"] = dict(id="z",name="Z",levels=[])
        b=copy.deepcopy(a); b["data"]=dict(reversed(list(b["data"].items())))
        x,y=self.normalized(a),self.normalized(b)
        self.assertEqual(build(x,x,set()),build(y,y,set()))

    def test_duplicate_level(self):
        a=fixture(); a["data"]["s"]["levels"]*=2
        with self.assertRaises(ValueError): self.normalized(a)

    def test_duplicate_item(self):
        a=fixture(); a["data"]["s"]["levels"][0]["itemRequirements"]*=2
        with self.assertRaises(ValueError): self.normalized(a)

    def test_ids_counts(self):
        for value in (0,-1,True,1.5,2**63):
            a=fixture(); a["data"]["s"]["levels"][0]["itemRequirements"][0]["count"]=value
            with self.assertRaises(ValueError): self.normalized(a)
        a=fixture(); a["data"]["s"]["id"]=""
        with self.assertRaises(ValueError): self.normalized(a)

    def test_variants(self):
        a=self.normalized(); b=copy.deepcopy(a)
        b["item_requirements"][0]=("s-1","item",4)
        out,meta=build(a,b,set())
        self.assertGreater(meta["regularPveStructuralDifferenceCount"],0)
        self.assertIn("pve\ts-1\titem\t4",out["hideout_item_requirements.tsv"])
        self.assertEqual(meta["missingItemIds"],["item"])
        self.assertEqual(build(a,a,set())[1]["structureModes"],["regular"])

    def test_utf8_escape(self):
        s=escape("中\t文\n\\\r")
        self.assertEqual(s.encode().decode(),"中\\t文\\n\\\\\\r")
        with self.assertRaises(ValueError): escape("\0")
        with self.assertRaises(UnicodeError): escape("\ud800")

    def test_missing_prerequisite(self):
        a=fixture(); a["data"]["s"]["levels"][0]["stationLevelRequirements"]=[dict(station="missing",level=1)]
        with self.assertRaises(ValueError): self.normalized(a)

    def test_trader_semantics(self):
        a=fixture(); a["data"]["s"]["levels"][0]["traderRequirements"][0]["requirementType"]="reputation"
        with self.assertRaises(ValueError): self.normalized(a)

if __name__=="__main__": unittest.main()
