from xml.etree import ElementTree

import svg.track_utils as track_utils


SVG_NAMESPACE = "http://www.w3.org/2000/svg"
INKSCAPE_NAMESPACE = "http://www.inkscape.org/namespaces/inkscape"
ElementTree.register_namespace("", SVG_NAMESPACE)
ElementTree.register_namespace("inkscape", INKSCAPE_NAMESPACE)


def create_path(path_data, name, color='#ff0'):
    path = ElementTree.Element(f"{{{SVG_NAMESPACE}}}path")
    path.set("d", path_data)
    path.set("style", f"stroke:none;fill:{color}")
    if name:
        path.set(f"{{{INKSCAPE_NAMESPACE}}}label", name)
    return path


def generate_track_svg(points, *, av=True, inicio_recto=True, fin_recto=True, vec_inicio=None, vec_fin=None, name=None):
    group = ElementTree.Element(f"{{{SVG_NAMESPACE}}}g")
    def gen_points(start, end):
        return track_utils.generate_path_from_coords(track_utils.generate_track_points(points, [start, end], inicio_recto=inicio_recto, fin_recto=fin_recto, vec_inicio=vec_inicio, vec_fin=vec_fin))[0]
    
    group.append(create_path(gen_points(-2.5, 2.5), 'track', '#ff0'))
    if av:
        group.append(create_path(gen_points(-1.25, 1.25), 'bv', '#000'))
    else:
        group2 = ElementTree.Element(f"{{{SVG_NAMESPACE}}}g")
        group2.set(f"{{{INKSCAPE_NAMESPACE}}}label", "bv")
        group.append(group2)
        group2.append(create_path(gen_points(-4.5, -2.5), 'bv_up', '#f0f'))
        group2.append(create_path(gen_points(2.5, 4.5), 'bv_down', '#f0f'))
    group.append(create_path(gen_points(-4.5, -3.5), 'bar_up', '#ff0'))
    group.append(create_path(gen_points(3.5, 4.5), 'bar_down', '#ff0'))
    group.append(create_path(gen_points(-3.5, -2.5), 'me_up', '#fff'))
    group.append(create_path(gen_points(2.5, 3.5), 'me_down', '#fff'))
    if name:
        group.set(f"{{{INKSCAPE_NAMESPACE}}}label", name)
    return group
