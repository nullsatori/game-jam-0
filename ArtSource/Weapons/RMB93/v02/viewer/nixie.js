import {MeshPhysicalMaterial,MeshStandardMaterial,Vector2} from 'three';

export const NIXIE_DEFAULTS={depthMetres:.0011,ghostStrength:.06,intensity:4.4,dischargeStrength:.48,haloStrength:.055,flickerStrength:.006,glassCurvature:.30};

export function installNixieMaterial(source,uniforms){
  const material=new MeshPhysicalMaterial();
  MeshStandardMaterial.prototype.copy.call(material,source);
  material.defines={STANDARD:'',PHYSICAL:''};
  material.emissive.set(0x000000);material.emissiveMap=null;
  // The counter remains opaque; a dielectric coat represents its outer glass.
  material.clearcoat=.9;material.clearcoatRoughness=.12;
  material.clearcoatNormalMap=material.normalMap;
  material.clearcoatNormalScale=new Vector2(.45,.45);
  material.ior=1.46;material.specularIntensity=.6;
  material.onBeforeCompile=shader=>{
    Object.assign(shader.uniforms,{
      uDigit:uniforms.digit,uEnabled:uniforms.enabled,uDigitAtlas:uniforms.atlas,
      uCounterMasks:uniforms.masks,uNixieTime:uniforms.time,
      uNixieDepth:{value:NIXIE_DEFAULTS.depthMetres},
      uNixieGhostStrength:{value:NIXIE_DEFAULTS.ghostStrength},
      uNixieIntensity:{value:NIXIE_DEFAULTS.intensity},
      uNixieDischarge:{value:NIXIE_DEFAULTS.dischargeStrength},
      uNixieHalo:{value:NIXIE_DEFAULTS.haloStrength},
      uNixieFlicker:{value:NIXIE_DEFAULTS.flickerStrength},
      uNixieGlassCurvature:{value:NIXIE_DEFAULTS.glassCurvature}
    });
    shader.vertexShader='varying vec2 vCounterUV;\n'+shader.vertexShader;
    shader.vertexShader=shader.vertexShader.replace('#include <uv_vertex>','#include <uv_vertex>\nvCounterUV=uv;');
    shader.fragmentShader=`
      varying vec2 vCounterUV;
      uniform sampler2D uDigitAtlas,uCounterMasks;
      uniform float uDigit,uEnabled,uNixieTime,uNixieDepth,uNixieGhostStrength;
      uniform float uNixieIntensity,uNixieDischarge,uNixieHalo,uNixieFlicker,uNixieGlassCurvature;
      vec3 nixieWire(float index,vec2 uv){
        vec2 cell=vec2(mod(index,4.0),floor(index/4.0));
        vec2 atlasUV=(cell*256.0+16.5+clamp(uv,0.0,1.0)*223.0)/1024.0;
        float inside=step(0.0,uv.x)*step(uv.x,1.0)*step(0.0,uv.y)*step(uv.y,1.0);
        return texture2D(uDigitAtlas,atlasUV).rgb*inside;
      }
    `+shader.fragmentShader;
    shader.fragmentShader=shader.fragmentShader.replace('#include <emissivemap_fragment>',`
      // Reconstruct UV tangent vectors in view space; this also respects rotated faces.
      vec3 nxDPX=dFdx(-vViewPosition),nxDPY=dFdy(-vViewPosition);
      vec2 nxDUX=dFdx(vCounterUV),nxDUY=dFdy(vCounterUV);
      float nxDet=nxDUX.x*nxDUY.y-nxDUX.y*nxDUY.x;
      float nxSafeDet=(nxDet<0.0?-1.0:1.0)*max(abs(nxDet),1e-12);
      vec3 nxT=(nxDPX*nxDUY.y-nxDPY*nxDUX.y)/nxSafeDet;
      vec3 nxB=(nxDPY*nxDUX.x-nxDPX*nxDUY.x)/nxSafeDet;
      vec3 nxV=normalize(vViewPosition);
      vec3 nxN=normalize(cross(nxT,nxB));
      #ifdef USE_CLEARCOAT
        // Optical curvature belongs only to the outer lens; cathode art stays flat.
        vec2 nxLensSlope=(vCounterUV-.5)*uNixieGlassCurvature;
        clearcoatNormal=normalize(clearcoatNormal+normalize(nxT)*nxLensSlope.x+normalize(nxB)*nxLensSlope.y);
      #endif
      float nxFacing=max(abs(dot(nxV,nxN)),.3);
      vec2 nxRay=vec2(dot(nxV,normalize(nxT))/max(length(nxT),.0001),dot(nxV,normalize(nxB))/max(length(nxB),.0001))/nxFacing;
      // Virtual glass depth in metres. Clamp grazing-view displacement inside the aperture.
      vec2 nxParallax=clamp(nxRay*uNixieDepth,vec2(-.10),vec2(.10));
      vec2 nxActiveUV=vCounterUV-nxParallax;
      vec2 nxBackUV=vCounterUV-nxParallax*2.25;
      float nxAperture=texture2D(uCounterMasks,vCounterUV).r;
      vec3 nxWire=nixieWire(uDigit,nxActiveUV);

      // Four independent unlit wire cathodes behind the active wire, at different depths.
      // They retain their shapes when power is OFF and never contribute to Emissive.
      float nxGhost0=nixieWire(0.0,vCounterUV-nxParallax*1.35+vec2(.002,-.003)).r*step(.5,abs(uDigit));
      float nxGhost1=nixieWire(2.0,vCounterUV-nxParallax*1.65+vec2(-.003,.002)).r*step(.5,abs(uDigit-2.0));
      float nxGhost2=nixieWire(7.0,vCounterUV-nxParallax*1.95+vec2(.003,.004)).r*step(.5,abs(uDigit-7.0));
      float nxGhost3=nixieWire(8.0,vCounterUV-nxParallax*2.25+vec2(-.004,.005)).r*step(.5,abs(uDigit-8.0));
      float nxGhost=nxGhost0*.9+nxGhost1*.72+nxGhost2*.55+nxGhost3*.4;

      #ifdef USE_MAP
        vec3 nxBackColor=texture2D(map,clamp(nxBackUV,0.0,1.0)).rgb;
        diffuseColor.rgb=mix(diffuseColor.rgb,nxBackColor*.72,nxAperture);
      #endif
      diffuseColor.rgb+=nxAperture*nxGhost*uNixieGhostStrength*vec3(.80,.66,.49);
      // The sparse anode mesh is in BaseColor, recessed behind the cathodes, not luminous.
      roughnessFactor=clamp(roughnessFactor,.24,.48);
      float nxFlicker=1.0+uNixieFlicker*(.65*sin(uNixieTime*57.3)+.35*sin(uNixieTime*93.7));
      float nxSheath=max(nxWire.g-nxWire.r*.12,0.0);
      float nxGasVariation=.97+.03*sin(nxActiveUV.y*13.0+uNixieTime*.7);
      totalEmissiveRadiance=nxAperture*uEnabled*uNixieIntensity*nxFlicker*(
        pow(nxWire.r,1.10)*vec3(1.0,.34,.065)
        +nxSheath*uNixieDischarge*nxGasVariation*vec3(1.0,.095,.003)
        +nxWire.b*uNixieHalo*vec3(1.0,.035,.001));
    `);
  };
  material.customProgramCacheKey=()=> 'rmb93-layered-nixie-v2';
  material.needsUpdate=true;
  return material;
}
