#version 330 core
out vec4 FragColor;

in vec3 particleColor;
in float particleLife;
in float particleId;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform int lightType;

float hash(float n) {
    return fract(sin(n) * 43758.5453123);
}

void main()
{
    vec2 coord = gl_PointCoord - vec2(0.5);
    
    float angle = hash(particleId) * 6.28318;
    float c = cos(angle);
    float s = sin(angle);
    vec2 rotated = vec2(coord.x * c - coord.y * s, coord.x * s + coord.y * c);
    
    vec2 absCoord = abs(rotated);
    float shape = max(absCoord.x, absCoord.y);
    
    float edgeNoise = hash(particleId * 7.123 + absCoord.x * 10.0) * 0.3;
    float edgeNoise2 = hash(particleId * 13.456 + absCoord.y * 10.0) * 0.3;
    shape += edgeNoise + edgeNoise2;
    
    if(shape > 0.5)
        discard;
    
    float innerShape = max(absCoord.x * 0.6, absCoord.y * 0.6);
    float brightness = 1.0 - smoothstep(0.0, 0.5, shape);
    brightness = pow(brightness, 1.5);
    
    if(innerShape < 0.15) {
        brightness *= 1.5;
    }
    
    vec3 finalColor = particleColor * brightness;
    
    if(lightType == 0) {
        float pulse = sin(length(lightPos) * 0.05 + particleId * 0.1) * 0.5 + 0.5;
        float lightBrightness = 0.3 + pulse * 1.5;
        finalColor *= lightBrightness;
        finalColor += vec3(0.2, 0.1, 0.3) * pulse;
    }
    else if(lightType == 1) {
        vec3 lightDir = normalize(lightPos);
        vec3 normal = normalize(vec3(coord * 2.0, sqrt(max(0.0, 1.0 - dot(coord, coord)))));
        float diff = max(dot(normal, lightDir), 0.0);
        diff = pow(diff, 2.0);
        
        vec3 ambient = particleColor * 0.15;
        vec3 diffuse = particleColor * diff * 2.5;
        finalColor = ambient + diffuse;
        
        if(diff > 0.7) {
            finalColor += vec3(0.3, 0.2, 0.5) * (diff - 0.7) * 3.0;
        }
    }
    else {
        vec3 particleWorldPos = viewPos;
        float distToLight = length(lightPos - particleWorldPos) * 0.02;
        float attenuation = 1.0 / (1.0 + distToLight * distToLight * 0.5);
        
        vec3 lightDir = normalize(lightPos - particleWorldPos);
        vec3 normal = normalize(vec3(coord * 2.0, sqrt(max(0.0, 1.0 - dot(coord, coord)))));
        float diff = max(dot(normal, lightDir), 0.0);
        
        vec3 ambient = particleColor * 0.1;
        vec3 lit = particleColor * (0.5 + diff * 1.5) * attenuation;
        finalColor = ambient + lit;
        
        if(attenuation > 0.5) {
            finalColor += vec3(0.4, 0.3, 0.6) * (attenuation - 0.5) * 2.0;
        }
    }
    
    finalColor = clamp(finalColor, 0.0, 2.0);
    
    float alpha = brightness * particleLife * 0.8;
    
    FragColor = vec4(finalColor, alpha);
}
