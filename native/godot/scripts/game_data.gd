# game_data.gd - loads the neutral tables in res://data. They were exported from
# the removed browser reference and are now the source of truth.
class_name GameData
extends RefCounted


static func load_json(name: String) -> Dictionary:
	var path := "res://data/%s.json" % name
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_error("GameData: cannot open %s" % path)
		return {}
	var parsed = JSON.parse_string(f.get_as_text())
	return parsed if parsed is Dictionary else {}


static func vehicle(vehicles: Dictionary, index: int) -> Dictionary:
	var list: Array = vehicles.get("vehicles", [])
	return list[posmod(index, list.size())] if not list.is_empty() else {}
