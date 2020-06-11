#version 330
##ubo

out vec4 outputColor;
smooth in vec3 color_to_fragment;
smooth in float striper_to_fragment;
smooth in float alpha_to_fragment;
smooth in vec2 round_pos_to_fragment;
flat in int flags_to_fragment;

void main() {
  float my_alpha = alpha;
  bool in_border = abs(round_pos_to_fragment.x) > 1 || abs(round_pos_to_fragment.y) > 1;
  if(in_border && layer_flags != 3) { // and not in stencil mode
    my_alpha = 1;
  }
  else { //filled area
    if(mod(striper_to_fragment,20)>10 || layer_flags==0) {
      discard;
    }
  }
  outputColor = vec4(color_to_fragment, my_alpha);
}
