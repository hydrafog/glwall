#define PI 3.141

float dis(float n) 
{ 
  return fract(sin(n)*1.50); 
}

void main () 
{
  float Id = dis(vertexId);
  float fr = dis(Id);
  
   
  float tex = texture(sound, vec2(fr, 0.0)).r;
  
  tex = pow(tex, 1.5);
  
  float pang = vertexId;
  float t = (time * (fr + .1)) * 10.;
  float x = Id * sin(pang + t);
  float y = Id * cos(pang + t);
  
   
  y += 0.5 * tex * (1. - abs(y));
  y *= .78;
  
  float sizeAfter = 0.5 + tex * 2.0;
  
  gl_Position = vec4(x, y, 0., 1.);
  gl_PointSize = 4. + 12. * tex;
  
   
  v_color = vec4(tex, tex * 0.5, 1.0 - tex, 0.8);
}
