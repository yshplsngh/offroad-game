# ground_textures.gd - loads the baked grass detail (generated/ground.bin,
# written by worldcore_bake) into mipmapped GPU textures, once at startup.
class_name GroundTextures
extends RefCounted


## Sets grass_albedo / grass_normal on `material`. Returns false if the bake is missing.
static func apply(material: ShaderMaterial, path := "res://generated/ground.bin") -> bool:
	var bytes := FileAccess.get_file_as_bytes(path)
	if bytes.size() < 12 or bytes.slice(0, 8).get_string_from_ascii() != "RLGRND01":
		push_error("GroundTextures: cannot load %s (run the worldcore_bake build step)" % path)
		return false
	var size := bytes.decode_u32(8)
	var n := size * size * 4
	if bytes.size() != 12 + n * 2:
		push_error("GroundTextures: %s has the wrong size" % path)
		return false
	material.set_shader_parameter("grass_albedo", _texture(bytes.slice(12, 12 + n), size))
	material.set_shader_parameter("grass_normal", _texture(bytes.slice(12 + n, 12 + n * 2), size))
	return true


static func _texture(data: PackedByteArray, size: int) -> ImageTexture:
	var image := Image.create_from_data(size, size, false, Image.FORMAT_RGBA8, data)
	image.generate_mipmaps()
	return ImageTexture.create_from_image(image)
