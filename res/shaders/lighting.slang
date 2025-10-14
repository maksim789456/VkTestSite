module lighting;

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT       -1

public struct Light
{
    public float4 position; // .xyz = position, .w = light type
    public float4 color; // .rgb = color, .w = intensity
    public float4 direction; // .xyz = light direction or vector, .w = constant attenuation (for point/spot)
    public float4 info; // .x/.y = inner/outer cone angle (for spotlights), .z = linear attenuation, .w = exp attenuation

    public float3 apply(float3 pos, float3 normal)
    {
        int type = position.w;
        switch (type) {
          case LIGHT_DIRECTIONAL:
            return applyDirectional(pos, normal);
          case LIGHT_POINT:
          case LIGHT_SPOT:
            return applyPointSpot(pos, normal);
      }

      return float3(0.0);
    }

   private float3 applyDirectional(float3 pos, float3 normal)
   {
      float3 N = normalize(normal);
      float3 L = normalize(-direction.xyz);
      float nDotL = max(dot(N, L), 0.0);
      return nDotL * color.w * color.rgb;
   }

   private float3 applyPointSpot(float3 pos, float3 normal)
   {
      float3 result = float3(0.0);

      float3 L = position.xyz - pos;
      float3 N = normalize(normal);
      float dist = length(L);
      L = normalize(L);

      float atten = attenuationCalc(dist);
      float nDotL = max(dot(N, L), 0.0);

      result = nDotL * atten * color.w * color.rgb;

      if (position.w == LIGHT_SPOT)
      {
          float rho = dot(L, normalize(-direction.xyz));
          float phi = info.x;   // inner cone cosine
          float theta = info.y; // outer cone cosine
          float spotFactor = saturate((rho - phi) / (phi - theta));
          result *= spotFactor;
      }

      return result;
   }

   private float attenuationCalc(float distance)
   {
      float constant = direction.w;
      float linear = info.z;
      float exp = info.w;

      return 1.0 / (constant + linear * distance + exp * (distance * distance));
   }

   public float computeLightRadius(float threshold)
   {
      float t = threshold / color.w;
      float constant = direction.w;
      float linear = info.z;
      float exp = info.w;

      float disc = linear * linear - 4.0*exp*(constant - 1.0/t);
      if (disc < 0.0)
        return 0.0;
      return (-linear + sqrt(disc)) / (2.0 * exp);
   }
}