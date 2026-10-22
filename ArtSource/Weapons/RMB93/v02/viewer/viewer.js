import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { getPoseDelta,POSES } from './poses.js';
import { installNixieMaterial,NIXIE_DEFAULTS } from './nixie.js';

const params = new URLSearchParams(location.search);
const renderer = new THREE.WebGLRenderer({antialias:true, preserveDrawingBuffer:true, powerPreference:'high-performance'});
renderer.setPixelRatio(1);
renderer.toneMapping=THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure=1;
renderer.outputColorSpace=THREE.SRGBColorSpace;
renderer.info.autoReset=false;
document.body.prepend(renderer.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color('#424640');
const pmrem = new THREE.PMREMGenerator(renderer);
const room = new RoomEnvironment();
// Broad studio cards exist only inside the reflection capture, never in the asset.
// A low frontal card reveals receiver planes; the elevated strip shapes the tubes.
function reflectionCard(position,width,height,radiance,target=room){
  const card=new THREE.Mesh(new THREE.PlaneGeometry(width,height),new THREE.MeshBasicMaterial({color:new THREE.Color(...radiance),side:THREE.DoubleSide,toneMapped:false}));
  card.position.set(...position);card.lookAt(0,0,0);target.add(card);return card;
}
reflectionCard([-1.5,4,-5],10,2,[4,3.9,3.7]);
reflectionCard([1,.35,-6],8,3.5,[1.05,1.10,1.13]);
reflectionCard([-2,2,5],7,1.5,[2,2.15,2.2]);
const environment = pmrem.fromScene(room, .04);
// An independent dark capture gives the lens exactly one restrained softbox,
// without inheriting the studio room's windows or its other reflection cards.
const counterRoom=new THREE.Scene();counterRoom.background=new THREE.Color(.12,.12,.12);
const lensSoftbox=reflectionCard([-3.64,3.48,-.75],.55,1.45,[.4,.4,.4],counterRoom);
const counterEnvironment=pmrem.fromScene(counterRoom,.04);
lensSoftbox.geometry.dispose();lensSoftbox.material.dispose();counterRoom.clear();
scene.environment=environment.texture;
scene.environmentIntensity=.8;
room.dispose(); pmrem.dispose();
const key = new THREE.DirectionalLight(0xfff4e5,1.9); key.position.set(.2,.6,-.55); scene.add(key);
const fill = new THREE.DirectionalLight(0xdce4e6,.85);fill.position.set(-.3,.08,-.8);scene.add(fill);
const ambientFill = new THREE.HemisphereLight(0xb8c4c9,0x373a33,.28);scene.add(ambientFill);
const rim = new THREE.DirectionalLight(0xc4d0cb,1.0); rim.position.set(-.4,.4,.3);scene.add(rim);
const orthographicCamera = new THREE.OrthographicCamera(-1,1,1,-1,.001,30);
const perspectiveCamera = new THREE.PerspectiveCamera(55,1,.002,30);
let camera=orthographicCamera;
const controls = new OrbitControls(camera,renderer.domElement);
controls.enableDamping = true; controls.dampingFactor=.12;
const gl=renderer.getContext();
const colorSampleCounts=Array.from(gl.getInternalformatParameter(gl.RENDERBUFFER,gl.RGBA16F,gl.SAMPLES)||[]);
const depthSampleCounts=Array.from(gl.getInternalformatParameter(gl.RENDERBUFFER,gl.DEPTH_COMPONENT24,gl.SAMPLES)||[]);
const composerSamples=Math.max(0,...colorSampleCounts.filter(n=>n<=4&&depthSampleCounts.includes(n)));
const composerTarget=new THREE.WebGLRenderTarget(1,1,{type:THREE.HalfFloatType,samples:composerSamples});
const composer = new EffectComposer(renderer,composerTarget);
const renderPass=new RenderPass(scene,camera);
composer.addPass(renderPass);
const bloom = new UnrealBloomPass(new THREE.Vector2(1672,941),.12,.25,1.05); composer.addPass(bloom);
composer.addPass(new OutputPass());
const status = document.querySelector('#status');
let model, counter, span=.7, center=new THREE.Vector3(.2,0,0), elapsed=[], previous=0, tick=0, detailBounds=null;
const movingParts=[];
let loadAccessOpen=false,stockDeployed=false;
const atlasUniforms={digit:{value:4}, enabled:{value:1}, atlas:{value:null}, masks:{value:null}, time:{value:0}};
window.__metrics = {ready:false, frames:0, fps:0, width:0, height:0, triangles:0, msaaSamples:composerSamples, lightingPreset:'Neutral', error:null};
const lightingPresets={
  Neutral:{environment:.8,key:1.9,fill:.85,hemisphere:.28,rim:1,keyPosition:[.2,.6,-.55]},
  Hard:{environment:.25,key:2.8,fill:.12,hemisphere:.08,rim:.65,keyPosition:[.6,.16,-.35]}
};
function setLightPreset(name){
  const preset=lightingPresets[name];if(!preset)throw new Error('Unknown lighting preset: '+name);
  scene.environmentIntensity=preset.environment;
  model?.traverse(o=>{if(o.isMesh)(Array.isArray(o.material)?o.material:[o.material]).forEach(m=>{if(m.envMap===counterEnvironment.texture)m.envMapIntensity=preset.environment;});});
  key.intensity=preset.key;key.position.set(...preset.keyPosition);
  fill.intensity=preset.fill;ambientFill.intensity=preset.hemisphere;rim.intensity=preset.rim;
  window.__metrics.lightingPreset=name;
  document.querySelectorAll('[data-light]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.light===name)));
  resetPerformance();
}
document.addEventListener('visibilitychange',()=>{elapsed=[];previous=0;});
function resetPerformance(){elapsed=[];previous=0;Object.assign(window.__metrics,{fps:0,frameTimeMs:0,samples:0});}
function fitDetail(){
  if(!detailBounds||!camera.isOrthographicCamera)return;
  camera.updateMatrixWorld(true);
  const projected=new THREE.Box3();
  for(const x of [detailBounds.min.x,detailBounds.max.x])for(const y of [detailBounds.min.y,detailBounds.max.y])for(const z of [detailBounds.min.z,detailBounds.max.z])projected.expandByPoint(new THREE.Vector3(x,y,z).applyMatrix4(camera.matrixWorldInverse));
  const extent=projected.getSize(new THREE.Vector3());
  camera.zoom=Math.min((camera.right-camera.left)/Math.max(extent.x*1.18,.001),(camera.top-camera.bottom)/Math.max(extent.y*1.18,.001));
  camera.updateProjectionMatrix();
}

function size(w=innerWidth,h=innerHeight){
  if(window.__metrics.width!==w||window.__metrics.height!==h)resetPerformance();
  renderer.setSize(w,h); composer.setSize(w,h);
  const aspect=w/h;
  orthographicCamera.left=-span/2;orthographicCamera.right=span/2;orthographicCamera.top=span/aspect/2;orthographicCamera.bottom=-span/aspect/2;
  orthographicCamera.updateProjectionMatrix();
  perspectiveCamera.aspect=aspect;perspectiveCamera.updateProjectionMatrix();
  fitDetail();
  Object.assign(window.__metrics,{width:w,height:h});
}
function setView(view='left') {
  if(!model)return;
  resetPerformance();
  detailBounds=null;
  camera=view==='ads'?perspectiveCamera:orthographicCamera;
  controls.object=camera;renderPass.camera=camera;
  controls.target.copy(center); camera.up.set(0,1,0);
  if(view==='ads'){
    camera.fov=55;camera.position.set(-.40,.074,0);controls.target.set(.48,.074,0);
    camera.zoom=1;camera.updateProjectionMatrix();controls.update();
    window.__metrics.view='ads';window.__metrics.projection='perspective';return;
  }
  if(view==='deployed'){
    model.updateMatrixWorld(true);detailBounds=new THREE.Box3().setFromObject(model);
    detailBounds.getCenter(controls.target);
    camera.position.copy(controls.target).add(new THREE.Vector3(.12,.18,-1));
    camera.zoom=1;controls.update();fitDetail();
    window.__metrics.view='deployed';window.__metrics.projection='orthographic';return;
  }
  if(view==='foreend'||view==='grip'||view==='stock'||view==='buttpad'||view==='controls'||view==='receiver'){
    detailBounds=new THREE.Box3();
    const names=view==='receiver'?/^(Receiver_shell|Left_control_lever)$/i:view==='controls'?/^(Trigger_guard|Trigger|Left_control_lever)$/i:view==='foreend'?/^Foreend_/i:/^(Pistol_grip|Grip_)/i;
    // Current exports join editable parts by assembly. Select the authored part
    // region in closed-pose mesh coordinates, then respect its live pose matrix.
    const region=view==='receiver'?new THREE.Box3(new THREE.Vector3(-.060,0,-.025),new THREE.Vector3(.125,.070,.025)):view==='controls'?new THREE.Box3(new THREE.Vector3(-.045,-.038,-.027),new THREE.Vector3(.035,.031,.012)):view==='foreend'?new THREE.Box3(new THREE.Vector3(.042,-.018,-.025),new THREE.Vector3(.322,.030,.025)):new THREE.Box3(new THREE.Vector3(-.118,-.105,-.022),new THREE.Vector3(-.040,.023,.022));
    const assembly=view==='foreend'?'Action':'Body';
    model.updateMatrixWorld(true);
    model.traverse(o=>{
      if(!o.isMesh)return;
      if(view==='buttpad'){
        if(o.userData.assembly!=='Stock')return;
        if(/^(Folded_stock_front_bridge|Folded_stock_heel_pad)$/i.test(o.name))detailBounds.union(new THREE.Box3().setFromObject(o));
        const vertices=o.geometry.getAttribute('position'),point=new THREE.Vector3(),localBounds=new THREE.Box3();
        for(let i=0;i<vertices.count;i++){point.fromBufferAttribute(vertices,i);if(point.x>.395)localBounds.expandByPoint(point);}
        if(!localBounds.isEmpty()){
          // Include the last ~4 cm of the arms even when their long faces have
          // no intermediate vertices in the joined production export.
          localBounds.min.x=Math.min(localBounds.min.x,.365);
          detailBounds.union(localBounds.applyMatrix4(o.matrixWorld));
        }
        return;
      }
      if(view==='stock'){
        if(o.userData.assembly==='Stock')detailBounds.union(new THREE.Box3().setFromObject(o));
        return;
      }
      if(names.test(o.name)){detailBounds.union(new THREE.Box3().setFromObject(o));return;}
      if(o.userData.assembly!==assembly)return;
      if(view==='controls'||view==='receiver')detailBounds.union(region.clone().applyMatrix4(o.matrixWorld));
      const vertices=o.geometry.getAttribute('position'),point=new THREE.Vector3();
      for(let i=0;i<vertices.count;i++){point.fromBufferAttribute(vertices,i);if(region.containsPoint(point))detailBounds.expandByPoint(point.applyMatrix4(o.matrixWorld));}
    });
    if(!detailBounds.isEmpty()){
      if(view==='stock'){
        // Keep the stationary hinge context visible in both folded/access poses.
        const hinge=new THREE.Vector3(...POSES.Stock.pivot),margin=new THREE.Vector3(.016,.016,.016);
        detailBounds.expandByPoint(hinge.clone().sub(margin));detailBounds.expandByPoint(hinge.clone().add(margin));
      }
      detailBounds.getCenter(controls.target);
      const direction=view==='receiver'?new THREE.Vector3(.23,.18,-1):view==='controls'?new THREE.Vector3(.18,.12,-1):view==='buttpad'?new THREE.Vector3(1,.18,-.38).transformDirection(getPoseDelta('Stock',stockDeployed||loadAccessOpen?1:0,stockDeployed?POSES.Stock.deployedAngle:undefined)):view==='stock'?(stockDeployed?new THREE.Vector3(-1,.22,-1):loadAccessOpen?new THREE.Vector3(-1,.35,-1):new THREE.Vector3(-.42,1.1,-1)):view==='foreend'?new THREE.Vector3(.48,.28,-1):new THREE.Vector3(-.45,.22,-1);
      camera.position.copy(controls.target).add(direction);camera.zoom=1;controls.update();fitDetail();
      window.__metrics.view=view;window.__metrics.projection='orthographic';return;
    }
    detailBounds=null;
  }
  const offsets={left:[0,.034,-1],right:[0,.034,1],front:[1,0,0],rear:[-1,0,0],top:[0,1,-.001],bottom:[0,-1,-.001],hero:[.45,.23,-1]};
  const offset=offsets[view]||offsets.left;
  camera.position.copy(center).add(new THREE.Vector3(...offset));
  if(view==='top')camera.up.set(0,0,1);
  if(view==='bottom')camera.up.set(0,0,-1);
  camera.zoom=1;camera.updateProjectionMatrix();controls.update();
  window.__metrics.view=view;
  window.__metrics.projection='orthographic';
}
function counterZoom(){
  if(!counter)return;
  resetPerformance();
  detailBounds=null;
  camera=perspectiveCamera;controls.object=camera;renderPass.camera=camera;
  new THREE.Box3().setFromObject(counter).getCenter(controls.target);
  counter.updateWorldMatrix(true,false);
  const n=counter.geometry.getAttribute('normal');
  const normal=new THREE.Vector3(n.getX(0),n.getY(0),n.getZ(0)).applyNormalMatrix(new THREE.Matrix3().getNormalMatrix(counter.matrixWorld));
  camera.up.set(0,1,0);camera.position.copy(controls.target).addScaledVector(normal,.12);
  camera.fov=40;camera.zoom=1;camera.updateProjectionMatrix();controls.update();
  window.__metrics.view='counter';window.__metrics.projection='perspective';
}
function setDigit(index=4) {
  index=Math.round(Number(index));if(index<0||index>10)index=10;
  atlasUniforms.digit.value=index;atlasUniforms.enabled.value=index===10?0:1;
  document.querySelector('#digit').value=String(index);window.__metrics.digit=index;
}
function applyAssemblyPose(assembly,value,angleOverride){
  const delta=getPoseDelta(assembly,value,angleOverride);
  for(const part of movingParts){
    if(part.assembly!==assembly)continue;
    part.object.parent.updateWorldMatrix(true,false);
    const local=new THREE.Matrix4().copy(part.object.parent.matrixWorld).invert().multiply(delta).multiply(part.baseWorld);
    local.decompose(part.object.position,part.object.quaternion,part.object.scale);
    part.object.updateMatrixWorld(true);
  }
}
function setPump(value=0){
  value=loadAccessOpen?0:Math.min(1,Math.max(0,Number(value)||0));applyAssemblyPose('Action',value);
  document.querySelector('#pump').value=String(value);window.__metrics.pump=value;
  window.__metrics.actionTravelMetres=.055*value;
}
function setLoadAccess(open=false){
  stockDeployed=false;window.__metrics.stockDeployed=false;
  document.querySelector('#stockDeployed').setAttribute('aria-pressed','false');
  loadAccessOpen=!!open;
  if(loadAccessOpen)setPump(0);
  document.querySelector('#pump').disabled=loadAccessOpen||!movingParts.some(p=>p.assembly==='Action');
  applyAssemblyPose('Stock',loadAccessOpen?1:0);applyAssemblyPose('LoadingLid',loadAccessOpen?1:0);
  document.querySelector('#loadAccess').textContent=loadAccessOpen?'Закрыть доступ':'Доступ к загрузке';
  window.__metrics.loadAccessOpen=loadAccessOpen;
}
function setStockDeployed(deployed=false){
  if(!movingParts.some(p=>p.assembly==='Stock'))return;
  setLoadAccess(false);setPump(0);
  stockDeployed=!!deployed;
  applyAssemblyPose('Stock',stockDeployed?1:0,POSES.Stock.deployedAngle);
  document.querySelector('#stockDeployed').setAttribute('aria-pressed',String(stockDeployed));
  window.__metrics.stockDeployed=stockDeployed;
  setView(stockDeployed?'deployed':'hero');
}
async function linearTexture(url){const t=await new THREE.TextureLoader().loadAsync(url);t.colorSpace=THREE.NoColorSpace;t.flipY=false;t.wrapS=t.wrapT=THREE.ClampToEdgeWrapping;return t;}
function installCounter(material){
  const lens=installNixieMaterial(material,atlasUniforms);
  lens.envMap=counterEnvironment.texture;lens.envMapIntensity=scene.environmentIntensity;
  return lens;
}

async function withCanvasSize(width,height,work){
  const original=renderer.getSize(new THREE.Vector2());
  try{size(width,height);return await work();}
  finally{size(original.x,original.y);}
}

window.__review={setView,setDigit,size,renderer,scene,controls,counterZoom,setPump,setLoadAccess,setStockDeployed,setLightPreset,
  get camera(){return camera;},
  capture:()=>{composer.render();return renderer.domElement.toDataURL('image/png');},
  saveCapture:()=>withCanvasSize(1672,941,async()=>{
    controls.update();composer.render();
    const metrics={...window.__metrics,captureTime:new Date().toISOString(),cameraPosition:camera.position.toArray(),cameraTarget:controls.target.toArray(),cameraZoom:camera.zoom,cameraType:camera.type,cameraFov:camera.isPerspectiveCamera?camera.fov:null,exposure:renderer.toneMappingExposure,environmentIntensity:scene.environmentIntensity,studioLighting:{key:key.intensity,keyPosition:key.position.toArray(),fill:fill.intensity,hemisphere:ambientFill.intensity,rim:rim.intensity,reflectionCards:1}};
    const result=await fetch('/api/capture',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({png:renderer.domElement.toDataURL('image/png'),metrics})});
    if(!result.ok)throw new Error('Capture save failed: '+result.status);
    return result.json();
  }),
  setLighting:({exposure,environmentIntensity,keyIntensity,fillIntensity,rimIntensity,background}={})=>{
    window.__metrics.lightingPreset='Custom';
    document.querySelectorAll('[data-light]').forEach(b=>b.setAttribute('aria-pressed','false'));
    if(exposure!==undefined)renderer.toneMappingExposure=exposure;
    if(environmentIntensity!==undefined)scene.environmentIntensity=environmentIntensity;
    if(environmentIntensity!==undefined)model?.traverse(o=>{if(o.isMesh)(Array.isArray(o.material)?o.material:[o.material]).forEach(m=>{if(m.envMap===counterEnvironment.texture)m.envMapIntensity=environmentIntensity;});});
    if(keyIntensity!==undefined)key.intensity=keyIntensity;
    if(fillIntensity!==undefined)fill.intensity=fillIntensity;
    if(rimIntensity!==undefined)rim.intensity=rimIntensity;
    if(background)scene.background.set(background);
  },
  captureMode:(value=true)=>document.body.classList.toggle('capture',value)
};

try {
  const manifest=await fetch('/api/assets').then(r=>r.json());
  const file=manifest.exports.find(p=>p.endsWith('Jam_RMB93_v02.glb'))||manifest.exports.find(p=>p.endsWith('.glb'));
  if(!file)throw new Error('GLB ещё не экспортирован. Обновите страницу после сборки.');
  const gltf=await new GLTFLoader().loadAsync('/'+file);model=gltf.scene;scene.add(model);
  model.updateMatrixWorld(true);
  model.traverse(o=>{if(o.isMesh&&['Action','Stock','LoadingLid'].includes(o.userData.assembly))movingParts.push({object:o,assembly:o.userData.assembly,baseWorld:o.matrixWorld.clone()});});
  const poseParts=Object.fromEntries(['Action','Stock','LoadingLid'].map(assembly=>[assembly,movingParts.filter(p=>p.assembly===assembly).length]));
  document.querySelector('#pump').disabled=!poseParts.Action;
  document.querySelector('#loadAccess').disabled=!(poseParts.Stock||poseParts.LoadingLid);
  document.querySelector('#stockDeployed').disabled=!poseParts.Stock;
  setPump(0);setLoadAccess(false);window.__metrics.poseParts=poseParts;
  const bounds=new THREE.Box3().setFromObject(model), dimensions=bounds.getSize(new THREE.Vector3());bounds.getCenter(center);
  center.y=-.010;
  span=dimensions.x/0.955;
  let triangles=0,meshCount=0;
  model.traverse(o=>{if(o.isMesh){meshCount++;triangles+=(o.geometry.index?o.geometry.index.count:o.geometry.attributes.position.count)/3;if(/counter/i.test(o.name)&& !/center|rim|hood/i.test(o.name))counter=o;}});
  const atlasPath=manifest.textures.find(p=>/DigitAtlas_M\.png$/i.test(p)), masksPath=manifest.textures.find(p=>/Counter_Masks\.png$/i.test(p));
  if(atlasPath&&masksPath){
    const [atlas,masks]=await Promise.all([linearTexture('/'+atlasPath),linearTexture('/'+masksPath)]);
    atlasUniforms.atlas.value=atlas;atlasUniforms.masks.value=masks;
    model.traverse(o=>{if(o.isMesh){if(Array.isArray(o.material))o.material=o.material.map(m=>/counter/i.test(m.name)?installCounter(m,atlas,masks):m);else if(/counter/i.test(o.material.name)){counter=o;o.material=installCounter(o.material,atlas,masks);}}});
  }
  size();setView(params.get('view')||'left');setDigit(params.get('digit')||4);
  if(params.has('capture'))document.body.classList.add('capture');
  Object.assign(window.__metrics,{ready:true,triangles,meshCount,model:file,dimensions:dimensions.toArray(),counterDynamic:!!atlasUniforms.atlas.value,counterShader:'layered-nixie-v2',nixieParameters:{...NIXIE_DEFAULTS},counterReflectionSoftboxes:1,cameraPosition:camera.position.toArray(),cameraTarget:controls.target.toArray(),span});
} catch(error){status.textContent=error.message;window.__metrics.error=error.message;console.error(error);}

document.querySelectorAll('[data-view]').forEach(b=>b.onclick=()=>setView(b.dataset.view));
document.querySelectorAll('[data-light]').forEach(b=>b.onclick=()=>setLightPreset(b.dataset.light));
document.querySelector('#digit').onchange=e=>setDigit(e.target.value);
document.querySelector('#pump').oninput=e=>setPump(e.target.value);
document.querySelector('#loadAccess').onclick=()=>setLoadAccess(!loadAccessOpen);
document.querySelector('#stockDeployed').onclick=()=>setStockDeployed(!stockDeployed);
document.querySelector('#resetPose').onclick=()=>{setPump(0);setLoadAccess(false);};
document.querySelector('#wire').onchange=e=>model?.traverse(o=>{if(o.isMesh)(Array.isArray(o.material)?o.material:[o.material]).forEach(m=>m.wireframe=e.target.checked);});
document.querySelector('#bloom').onchange=e=>bloom.enabled=e.target.checked;
document.querySelector('#exposure').oninput=e=>renderer.toneMappingExposure=Number(e.target.value);
document.querySelector('#counterZoom').onclick=counterZoom;
document.querySelector('#hide').onclick=()=>document.body.classList.add('capture');
document.querySelector('#saveCapture').onclick=async e=>{const button=e.target;button.disabled=true;try{await window.__review.saveCapture();button.textContent='Кадр сохранён';}catch(error){button.textContent='Ошибка сохранения';console.error(error);}finally{button.disabled=false;}};
addEventListener('keydown',e=>{if(e.key.toLowerCase()==='h')document.body.classList.toggle('capture');});
addEventListener('resize',()=>size());
size();
renderer.setAnimationLoop(now=>{
  atlasUniforms.time.value=now*.001;
  renderer.info.reset();controls.update();composer.render();
  if(previous&&document.visibilityState==='visible'){const delta=now-previous;if(delta<2000)elapsed.push(delta);if(elapsed.length>180)elapsed.shift();}
  previous=now;window.__metrics.frames++;
  if(++tick%30===0){
    const mean=elapsed.reduce((a,b)=>a+b,0)/Math.max(1,elapsed.length);
    Object.assign(window.__metrics,{fps:mean?1000/mean:0,frameTimeMs:mean,drawCalls:renderer.info.render.calls,samples:elapsed.length});
    if(window.__metrics.ready)status.textContent=`${Math.round(window.__metrics.triangles).toLocaleString('ru')} треугольников · ${window.__metrics.meshCount} частей\n${Math.round(window.__metrics.fps)} FPS · ${window.__metrics.width} × ${window.__metrics.height} · WebGL`;
  }
});
